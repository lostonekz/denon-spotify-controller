/*
 * The captive portal's HTTP surface. docs/NETWORKING.md §1.2's "captive
 * portal lists scanned networks" is scoped down deliberately for Phase 2:
 * the form takes a typed SSID instead of a live scan list. Scanning while
 * the SoftAP is active needs WIFI_MODE_APSTA plus the async scan-complete
 * event, and doesn't change what this component actually needs to prove
 * (SoftAP + portal + save + connect) -- Phase 2's real exit criteria are
 * entirely about OTA. A documented cut, not a silent one.
 */
#include "net_wifi_internal.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "net_wifi_portal_http";

#define FORM_BODY_MAX 256

static const char PAGE_FORM[] =
    "<!doctype html><html><head><title>Denon/Spotify Controller Setup</title></head>"
    "<body><h1>WiFi Setup</h1>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>Network name (SSID)<br><input name=\"ssid\" maxlength=\"32\" required></label><br><br>"
    "<label>Password<br><input name=\"psk\" type=\"password\" maxlength=\"63\"></label><br><br>"
    "<button type=\"submit\">Connect</button>"
    "</form></body></html>";

static const char PAGE_SAVED[] =
    "<!doctype html><html><body><h1>Saved</h1>"
    "<p>The device will now connect to your network.</p></body></html>";

static const char PAGE_SAVE_FAILED[] =
    "<!doctype html><html><body><h1>Save failed</h1><p>Please try again.</p></body></html>";

static void url_decode(char *s)
{
    char *out = s;
    while (*s != '\0') {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && s[1] != '\0' && s[2] != '\0') {
            char hex[3] = { s[1], s[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

/* Bare-bones application/x-www-form-urlencoded extractor for exactly the
 * two fields this form has -- not a general parser. */
static bool extract_form_field(const char *body, const char *key, char *out, size_t out_cap)
{
    size_t key_len = strlen(key);
    const char *p = body;

    while (p != NULL) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            const char *amp = strchr(val, '&');
            size_t val_len = (amp != NULL) ? (size_t)(amp - val) : strlen(val);
            if (val_len >= out_cap) {
                val_len = out_cap - 1;
            }
            memcpy(out, val, val_len);
            out[val_len] = '\0';
            url_decode(out);
            return true;
        }
        p = strchr(p, '&');
        if (p != NULL) {
            p++;
        }
    }
    return false;
}

static esp_err_t handle_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE_FORM, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_post_save(httpd_req_t *req)
{
    net_wifi_t *w = (net_wifi_t *)req->user_ctx;

    if (req->content_len == 0 || req->content_len >= FORM_BODY_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "form too large", HTTPD_RESP_USE_STRLEN);
    }

    char body[FORM_BODY_MAX];
    size_t total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, body + total, req->content_len - total);
        if (r <= 0) {
            return httpd_resp_send_500(req);
        }
        total += (size_t)r;
    }
    body[total] = '\0';

    char ssid[NET_WIFI_SSID_MAX] = { 0 };
    char psk[NET_WIFI_PSK_MAX]   = { 0 };
    bool have_ssid = extract_form_field(body, "ssid", ssid, sizeof(ssid));
    extract_form_field(body, "psk", psk, sizeof(psk));

    if (!have_ssid || ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "ssid is required", HTTPD_RESP_USE_STRLEN);
    }

    bool ok = net_wifi_credentials_save(&w->secure_kv, ssid, psk);

    httpd_resp_set_type(req, "text/html");
    esp_err_t send_result = httpd_resp_send(req, ok ? PAGE_SAVED : PAGE_SAVE_FAILED,
                                              HTTPD_RESP_USE_STRLEN);

    /* The response above already reflects the real, synchronous save
     * result; this just lets net_sup fold the outcome into the FSMs
     * (tear down the portal, start the STA connection) on its own task. */
    net_wifi_post_msg(w, NET_WIFI_MSG_PROV_CREDENTIALS_RESULT, ok);

    return send_result;
}

static esp_err_t handle_catch_all(httpd_req_t *req)
{
    /* Any other path -- including the phone OS's own captive-portal probe
     * URLs -- redirects to the setup form, which is what triggers the
     * "Sign in to network" prompt. */
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

void net_wifi_portal_http_start(net_wifi_t *w)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        w->last_action_ok = false;
        return;
    }

    httpd_uri_t root      = { .uri = "/",     .method = HTTP_GET,  .handler = handle_get_root,  .user_ctx = w };
    httpd_uri_t save      = { .uri = "/save", .method = HTTP_POST, .handler = handle_post_save, .user_ctx = w };
    httpd_uri_t catch_all = { .uri = "/*",    .method = HTTP_GET,  .handler = handle_catch_all, .user_ctx = w };

    /* Registration order matters: "/" is checked before the wildcard, so
     * an exact root request hits handle_get_root, not the catch-all. */
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &catch_all);

    w->http_server = server;
}

void net_wifi_portal_http_stop(net_wifi_t *w)
{
    if (w->http_server != NULL) {
        httpd_stop((httpd_handle_t)w->http_server);
        w->http_server = NULL;
    }
}
