/*
 * HTTP fallback transport. See denon_http.h for the KNOWN GAP note on
 * AppCommand.xml vs. the formiPhoneAppDirect.xml-based polling used here.
 * Target-only -- reuses net_http_get(), not host-testable.
 */
#include "denon_http.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "denon_http";

#define DENON_HTTP_POLL_INTERVAL_US ((int64_t)2 * 1000000)   /* NETWORKING.md §1.4's 2s cadence, per field -- see the KNOWN GAP note */
#define DENON_HTTP_RESP_BUF_LEN     256u

static const char *const POLL_FIELDS[] = { "MV", "PW", "MU", "SI", "MS" };
#define POLL_FIELD_COUNT (sizeof(POLL_FIELDS) / sizeof(POLL_FIELDS[0]))

static bool denon_http_open(void *vctx, const char *host)
{
    denon_http_ctx_t *ctx = (denon_http_ctx_t *)vctx;
    if (host == NULL || strlen(host) >= sizeof(ctx->host)) {
        return false;
    }
    strncpy(ctx->host, host, sizeof(ctx->host) - 1);
    ctx->host[sizeof(ctx->host) - 1] = '\0';
    ctx->poll_field_index = 0;
    ctx->next_poll_due_us = esp_timer_get_time();   /* due immediately -- no artificial cold-start delay */
    return true;
}

static bool denon_http_send_cmd(void *vctx, const char *cmd)
{
    denon_http_ctx_t *ctx = (denon_http_ctx_t *)vctx;
    if (ctx->host[0] == '\0' || cmd == NULL) {
        return false;
    }

    char url[128];
    int n = snprintf(url, sizeof(url), "http://%s:%d/goform/formiPhoneAppDirect.xml?%s",
                       ctx->host, DENON_HTTP_PORT, cmd);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return false;
    }

    char resp_buf[DENON_HTTP_RESP_BUF_LEN];
    net_http_response_t resp = { 0 };
    if (!net_http_get(ctx->http, url, &ctx->policy, resp_buf, sizeof(resp_buf), &resp)) {
        return false;
    }
    return resp.status_code == 200;
}

/* Defensive extraction: the query-style GET's response body is not a
 * documented schema (see the KNOWN GAP note in denon_http.h) -- look for
 * the field's own prefix at the start of the body and copy up to the first
 * byte that can't plausibly be part of a raw wire command (whitespace,
 * CR/LF, or '<' if the body turns out to be XML/HTML-wrapped after all). */
static bool extract_field_line(const char *field_prefix, const char *body, size_t body_len,
                                  char *out_line, size_t out_cap)
{
    size_t prefix_len = strlen(field_prefix);
    if (body_len < prefix_len || strncmp(body, field_prefix, prefix_len) != 0) {
        return false;
    }

    size_t i = 0;
    while (i < body_len && i < out_cap - 1) {
        char c = body[i];
        if (c == '\0' || c == '\r' || c == '\n' || c == '<' || isspace((unsigned char)c)) {
            break;
        }
        out_line[i] = c;
        i++;
    }
    if (i == 0) {
        return false;
    }
    out_line[i] = '\0';
    return true;
}

static denon_transport_poll_result_t denon_http_poll(void *vctx, char *out_line,
                                                        size_t out_cap, uint32_t timeout_ms)
{
    (void)timeout_ms;   /* HTTP never blocks in poll() -- either due now or not, see denon_transport.h */
    denon_http_ctx_t *ctx = (denon_http_ctx_t *)vctx;
    if (ctx->host[0] == '\0' || out_line == NULL || out_cap == 0) {
        return DENON_TRANSPORT_POLL_LOST;
    }

    int64_t now = esp_timer_get_time();
    if (now < ctx->next_poll_due_us) {
        return DENON_TRANSPORT_POLL_NOTHING;
    }

    const char *field = POLL_FIELDS[ctx->poll_field_index];
    ctx->poll_field_index = (ctx->poll_field_index + 1) % (int)POLL_FIELD_COUNT;
    ctx->next_poll_due_us = now + DENON_HTTP_POLL_INTERVAL_US;

    char url[128];
    int n = snprintf(url, sizeof(url), "http://%s:%d/goform/formiPhoneAppDirect.xml?%s",
                       ctx->host, DENON_HTTP_PORT, field);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return DENON_TRANSPORT_POLL_LOST;
    }

    char resp_buf[DENON_HTTP_RESP_BUF_LEN];
    net_http_response_t resp = { 0 };
    if (!net_http_get(ctx->http, url, &ctx->policy, resp_buf, sizeof(resp_buf), &resp)) {
        return DENON_TRANSPORT_POLL_LOST;
    }
    if (resp.status_code != 200) {
        return DENON_TRANSPORT_POLL_LOST;
    }

    if (extract_field_line(field, resp_buf, resp.len, out_line, out_cap)) {
        return DENON_TRANSPORT_POLL_LINE;
    }
    /* A 200 with a body we couldn't parse is not a dead link -- just
     * nothing usable this cycle. The next rotation tries the next field. */
    return DENON_TRANSPORT_POLL_NOTHING;
}

static void denon_http_close(void *vctx)
{
    denon_http_ctx_t *ctx = (denon_http_ctx_t *)vctx;
    ctx->host[0] = '\0';
}

denon_transport_if_t denon_http_transport(denon_http_ctx_t *ctx, net_http_t *http,
                                              const http_policy_t *policy)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->http   = http;
    ctx->policy = *policy;

    denon_transport_if_t iface = {
        .open     = denon_http_open,
        .send_cmd = denon_http_send_cmd,
        .poll     = denon_http_poll,
        .close    = denon_http_close,
        .ctx      = ctx,
    };
    return iface;
}

bool denon_http_probe_capabilities(net_http_t *http, const http_policy_t *policy, const char *host)
{
    if (host == NULL) {
        return false;
    }

    char url[96];
    int n = snprintf(url, sizeof(url), "http://%s:%d/goform/Deviceinfo.xml", host, DENON_HTTP_PORT);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return false;
    }

    char resp_buf[DENON_HTTP_RESP_BUF_LEN];
    net_http_response_t resp = { 0 };
    bool ok = net_http_get(http, url, policy, resp_buf, sizeof(resp_buf), &resp) &&
                resp.status_code == 200;

    /* RISKS.md R5: "a Denon capability probe at startup detects a changed
     * device rather than misbehaving" -- today that means logging, not
     * parsing model/firmware/zones out of the body. Extending this to
     * actually record capabilities is real future work, not done here. */
    if (ok) {
        ESP_LOGI(TAG, "capability probe ok (%s)", host);
    } else {
        ESP_LOGW(TAG, "capability probe failed (%s)", host);
    }
    return ok;
}
