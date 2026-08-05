#include "pair_http.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pair_http";

/* One request at a time, on one task, so file-static buffers are safe and
 * keep the httpd task's own 4 KB stack out of trouble --
 * svc_spotify_submit_pasted_url() plus svc_spotify_ctrl_submit() already put
 * ~1.25 KB of code/state/command buffers on whatever stack calls them. Same
 * single-owner reasoning debug_console_spotify.c uses for its static g_ctx. */
#define PAIR_BODY_MAX 1024
#define PAIR_URL_MAX  768

static httpd_handle_t s_server;
static app_ctx_t     *s_ctx;
static char           s_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX];
static char           s_body[PAIR_BODY_MAX];
static char           s_url[PAIR_URL_MAX];
static bool           s_logged_stack_hwm;

/* Duplicated from net_wifi_portal_http.c rather than exported from it. Those
 * copies are static, and net_wifi's own header comment calls them "not a
 * general parser"; promoting a throwaway form decoder into a component's
 * public API for one caller costs more than 40 lines do. This project already
 * has the same deliberate duplication for percent_encode() (spotify_auth.c /
 * spotify_authorize_url.c) with a comment saying why.
 *
 * The decode is not optional here: svc_spotify's own extract_query_param()
 * does NOT percent-decode, and a browser form-encodes the pasted URL's ? & =
 * : and / before it ever reaches us. */
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
                return false;   /* truncating a redirect URL would only produce a
                                     confusing "no code/state" error later */
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

static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Connect Spotify</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;padding:24px;max-width:34rem;"
    "background:#111;color:#eee;line-height:1.5}"
    "h1{font-size:1.5rem}a.go{display:block;background:#1DB954;color:#000;font-weight:600;"
    "text-decoration:none;padding:16px;border-radius:12px;text-align:center;margin:24px 0}"
    "input{width:100%;box-sizing:border-box;padding:12px;font-size:1rem;border-radius:8px;"
    "border:1px solid #444;background:#1c1c1c;color:#eee}"
    "button{width:100%;padding:16px;font-size:1rem;font-weight:600;margin-top:12px;"
    "border-radius:12px;border:0;background:#eee;color:#111}"
    ".note{color:#aaa;font-size:.9rem}.bad{color:#ff8f8a}"
    "</style></head><body>";

static const char PAGE_TAIL[] = "</body></html>";

static esp_err_t send_page(httpd_req_t *req, const char *banner, const char *body_html)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    if (banner != NULL) {
        httpd_resp_send_chunk(req, banner, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, body_html, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, PAGE_TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* The authorize URL goes into an href, so its '&' separators have to be
 * written as entities. Browsers are forgiving about this; validators and
 * anything that reparses the page are not, and "&state=" is close enough to a
 * real entity name to be worth not gambling on. */
static esp_err_t send_authorize_link(httpd_req_t *req)
{
    httpd_resp_send_chunk(req, "<a class=\"go\" href=\"", HTTPD_RESP_USE_STRLEN);
    const char *p = s_authorize_url;
    while (*p != '\0') {
        const char *amp = strchr(p, '&');
        size_t run = (amp != NULL) ? (size_t)(amp - p) : strlen(p);
        if (run > 0) {
            httpd_resp_send_chunk(req, p, run);
        }
        if (amp == NULL) {
            break;
        }
        httpd_resp_send_chunk(req, "&amp;", HTTPD_RESP_USE_STRLEN);
        p = amp + 1;
    }
    return httpd_resp_send_chunk(req, "\">Authorize with Spotify</a>", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_pair_page(httpd_req_t *req, const char *banner)
{
    if (s_authorize_url[0] == '\0') {
        return send_page(req, banner,
                         "<h1>Connect Spotify</h1>"
                         "<p class=\"bad\">This device isn't set up for Spotify yet -- it has no "
                         "client ID or redirect URI configured, so there's nothing to authorize "
                         "against.</p>");
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    if (banner != NULL) {
        httpd_resp_send_chunk(req, banner, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req,
                          "<h1>Connect Spotify</h1>"
                          "<p>1. Tap below and approve access.</p>", HTTPD_RESP_USE_STRLEN);
    send_authorize_link(req);
    httpd_resp_send_chunk(req,
                          "<p>2. The page you land on <b>will fail to load</b>. That is expected -- "
                          "nothing is listening at that address, by design.</p>"
                          "<p>3. Copy the whole address from your browser's address bar and paste it "
                          "here:</p>"
                          "<form method=\"POST\" action=\"/pair\">"
                          "<input name=\"url\" placeholder=\"http://127.0.0.1/callback?code=...\" "
                          "autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\">"
                          "<button type=\"submit\">Finish</button></form>"
                          "<p class=\"note\">Nothing you paste here leaves this device.</p>",
                          HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, PAGE_TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t handle_get_pair(httpd_req_t *req)
{
    /* Reads the cached URL only -- never regenerates. That is what makes
     * browser BACK-navigation, a re-scan of the QR, a second tab, and a
     * captive-portal probe all safe: regenerating here would mint a fresh
     * verifier and nonce, silently invalidating the code the user is at that
     * moment holding in their clipboard. */
    return send_pair_page(req, NULL);
}

static spotify_session_summary_t current_session_status(void)
{
    spotify_state_t sp = { 0 };
    core_state_read(&s_ctx->state, DOMAIN_SPOTIFY, &sp, sizeof(sp));
    return sp.session_status;
}

static esp_err_t handle_post_pair(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= PAIR_BODY_MAX) {
        return send_page(req, NULL, "<h1>Connect Spotify</h1>"
                                     "<p class=\"bad\">That didn't look like a redirect URL.</p>");
    }

    size_t total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, s_body + total, req->content_len - total);
        if (r <= 0) {
            return httpd_resp_send_500(req);
        }
        total += (size_t)r;
    }
    s_body[total] = '\0';

    if (!extract_form_field(s_body, "url", s_url, sizeof(s_url))) {
        return send_pair_page(req, "<p class=\"bad\">No URL was submitted.</p>");
    }

    /* Both checks below MUST happen before submitting, not after.
     * svc_spotify_submit_pasted_url() returns true in both of these cases --
     * the clock check because the resulting CODE_PASTED command is dropped
     * silently later on the spotify task, and the status check because
     * spotify_session_fsm ignores CODE_PASTED outside AWAITING_CODE with no
     * log at all. Submitting first and reporting success would be telling the
     * user their pairing worked when nothing happened. */
    if (!s_ctx->net_time.synced) {
        ESP_LOGW(TAG, "paste rejected: clock not yet synced");
        return send_pair_page(req,
                              "<p class=\"bad\">The panel is still setting its clock. Wait about ten "
                              "seconds and press Finish again.</p>");
    }

    if (current_session_status() != SPOTIFY_SESSION_PAIRING) {
        ESP_LOGW(TAG, "paste rejected: session is not awaiting a code");
        return send_pair_page(req,
                              "<p class=\"bad\">The panel isn't waiting for a code right now. Reopen "
                              "the pairing screen on the panel and scan again.</p>");
    }

    if (!svc_spotify_submit_pasted_url(&s_ctx->svc_spotify, s_url)) {
        ESP_LOGW(TAG, "paste rejected: no code/state, or state mismatch");
        return send_pair_page(req,
                              "<p class=\"bad\">That link has no code in it, or it's from an earlier "
                              "attempt. Use the Authorize link above to get a fresh one.</p>");
    }

    if (!s_logged_stack_hwm) {
        s_logged_stack_hwm = true;
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        /* Bytes on ESP-IDF, not words -- see svc_diagnostics.c's own note.
         * Measured on the real POST path: 1820 free of 4096, i.e. ~44%
         * headroom, comfortably over this project's 25% floor. That is why
         * the body/URL buffers here are file-static rather than on-stack. */
        ESP_LOGI(TAG, "httpd task stack_hwm=%u bytes after first POST",
                 (unsigned)uxTaskGetStackHighWaterMark(self));
    }

    ESP_LOGI(TAG, "code submitted, exchanging");
    return send_page(req, NULL,
                     "<h1>Finishing up</h1><p>Sent to the panel. Watch its screen -- it should say "
                     "connected within a few seconds.</p>"
                     "<p class=\"note\">You can close this page.</p>");
}

/* Same captive-portal trick net_wifi_portal_http.c uses: anything else lands
 * somewhere useful rather than on a 404. Registered last, and after the exact
 * matches, because httpd matches in registration order. */
static esp_err_t handle_catch_all(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/pair");
    return httpd_resp_send(req, NULL, 0);
}

/* Starting the server and deciding whether to begin a pairing exchange are
 * two separate things, and conflating them was a real bug: an early
 * `if (s_server != NULL) return true;` at the top of pair_http_start() made
 * the forced re-pair path DEAD. The confirm button only exists while the pair
 * overlay is open, and the overlay's own mount() has always already started
 * the server by then -- so every force=1 press hit that early return and did
 * nothing at all. On the panel it read as a button that isn't clickable.
 * The idempotency that guard was protecting (a double START must not mint a
 * fresh verifier and invalidate a code the user is holding) now lives on the
 * pairing decision below, where it belongs, keyed on the cached URL. */
static bool pair_http_ensure_server(app_ctx_t *ctx)
{
    if (s_server != NULL) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    /* Every one of these overrides the stock value deliberately.
     * net_wifi's captive portal takes the defaults (port 80, ctrl 32768) and
     * the two servers are only *usually* mutually exclusive -- a Wi-Fi drop
     * during pairing can start provisioning while this one is still up, and
     * LWIP_MAX_SOCKETS is 10 in total. Distinct ports and a small socket
     * budget make that overlap harmless instead of a silent bind failure. */
    config.server_port    = CONFIG_DSC_PAIR_HTTP_PORT;
    config.ctrl_port      = CONFIG_DSC_PAIR_HTTP_CTRL_PORT;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.core_id          = 0;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        s_server = NULL;
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return false;
    }

    static const httpd_uri_t uri_get   = { .uri = "/pair", .method = HTTP_GET,  .handler = handle_get_pair };
    static const httpd_uri_t uri_post  = { .uri = "/pair", .method = HTTP_POST, .handler = handle_post_pair };
    static const httpd_uri_t uri_other = { .uri = "/*",    .method = HTTP_GET,  .handler = handle_catch_all };
    httpd_register_uri_handler(s_server, &uri_get);
    httpd_register_uri_handler(s_server, &uri_post);
    httpd_register_uri_handler(s_server, &uri_other);

    /* Note what this does and does not buy. Both of svc_artwork's own buffers
     * (the cache and jpeg_staging) are MALLOC_CAP_SPIRAM, so suspending frees
     * no internal RAM directly. What it prevents is the TRANSIENT allocation
     * a fetch makes: net_http/mbedTLS wants 16.7 KB CONTIGUOUS internal for
     * its TLS record buffer, and this httpd's task stack and sockets come out
     * of that same internal pool via a plain xTaskCreate.
     * Measured on real hardware with the pairing page up: internal_free fell
     * from ~37 KB to ~17 KB, with a low-water mark of 7.9 KB -- and that is
     * WITH artwork suspended. A concurrent artwork fetch asking for 16.7 KB
     * contiguous out of that would not have fitted. Same treatment, and the
     * same reason, as the OTA relay's own suspend. */
    svc_artwork_request_suspend(&ctx->svc_artwork);

    ESP_LOGI(TAG, "pairing page listening on port %d", (int)config.server_port);
    return true;
}

bool pair_http_start(app_ctx_t *ctx, bool force)
{
    s_ctx = ctx;

    /* Server first, pairing second -- the panel's QR is gated on
     * session_status == PAIRING, so the other order could put a QR on screen
     * advertising a port that failed to bind. */
    if (!pair_http_ensure_server(ctx)) {
        return false;
    }

    /* The idempotency that matters: a repeat START must not mint a fresh
     * verifier and silently invalidate a code the user is at that moment
     * holding in their clipboard. Keyed on the cached URL, not on whether the
     * server happens to be running -- `force` is an explicit "give me a new
     * one", so it deliberately passes through. */
    if (!force && s_authorize_url[0] != '\0') {
        return true;
    }

    spotify_session_summary_t status = current_session_status();
    if (force && (status == SPOTIFY_SESSION_ACTIVE || status == SPOTIFY_SESSION_REFRESHING)) {
        /* The FSM has no AUTHORIZED->PAIRING edge, so a healthy session has
         * to be torn down first or the pair request is a silent no-op. The
         * ctrl queue is FIFO, so UNPAIR is processed before the PAIR that
         * begin_pair() enqueues below, and action_unpair() provably leaves
         * pending_pair/pending_state alone -- so the nonce written below
         * survives the unpair that precedes it. */
        ESP_LOGI(TAG, "forced re-pair: unpairing the current session first");
        svc_spotify_ctrl_submit(&ctx->svc_spotify, SPOTIFY_CTRL_CMD_UNPAIR, NULL);
        status = SPOTIFY_SESSION_UNPAIRED;
    }

    if (status == SPOTIFY_SESSION_UNPAIRED || status == SPOTIFY_SESSION_REAUTH_REQUIRED) {
        if (!svc_spotify_begin_pair(&ctx->svc_spotify, s_authorize_url)) {
            s_authorize_url[0] = '\0';
            ESP_LOGE(TAG, "begin_pair failed -- check client_id/redirect_uri");
        } else {
            /* First 40 chars only: enough to confirm on a serial log that
             * exactly ONE URL was generated per overlay-open (the whole point
             * of caching it), without putting a live authorization request
             * complete with its CSRF nonce into the log. */
            ESP_LOGI(TAG, "pairing started, authorize url: %.40s...", s_authorize_url);
        }
    } else {
        /* Already connected and not forced: the page still serves, but with
         * no link -- the panel is showing a confirm button instead. */
        s_authorize_url[0] = '\0';
    }

    return true;
}

void pair_http_stop(app_ctx_t *ctx)
{
    if (s_server == NULL) {
        return;
    }

    /* Worth a loud line: the user consented on their phone but closed the
     * panel screen before pasting, so the FSM is left in AWAITING_CODE and
     * their phone is about to POST to a socket that no longer exists. */
    if (current_session_status() == SPOTIFY_SESSION_PAIRING) {
        ESP_LOGW(TAG, "pairing page closed while still awaiting a code -- session left pairing");
    }

    httpd_stop(s_server);
    s_server = NULL;
    /* Not a secret on its own, but it carries the live CSRF nonce, and there
     * is no reason for it to sit in RAM until the next pairing overwrites it. */
    memset(s_authorize_url, 0, sizeof(s_authorize_url));

    svc_artwork_request_resume(&ctx->svc_artwork);
    ESP_LOGI(TAG, "pairing page stopped");
}
