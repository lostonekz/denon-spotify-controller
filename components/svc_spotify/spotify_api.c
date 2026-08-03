#include "spotify_api.h"

#include <stdio.h>

#include "esp_log.h"

#define SPOTIFY_API_BASE "https://api.spotify.com/v1/me/player"

static const char *TAG = "spotify_api";

/* Real bug, found live (Tier 7): a fixed 4096-byte stack buffer for
 * /v1/me/player's response -- ADR-0010's own words describe this exact
 * response as "large (several KB), deeply nested... third-party, and
 * evolving," yet nothing had ever actually measured it against a real
 * account. A real response (device object, full track+artists[]+album
 * with a multi-resolution images[] array, context, actions, ...)
 * routinely exceeds 4096 bytes. net_http_request()'s own contract is
 * silent truncation, not an error -- fine for artwork's binary JPEG
 * stream, wrong for JSON, where a truncated body is just invalid input
 * cJSON fails to parse, and a failed parse leaves DOMAIN_PLAYBACK holding
 * whatever the last successful poll wrote (spotify_service_task.c's
 * handle_poll_result() only writes core_state on success) -- explaining
 * a real symptom seen live: the progress bar kept interpolating (stale
 * position_ms + is_playing from one earlier, smaller, successfully-
 * parsed response) while title/artist/artwork never appeared. Not a
 * bigger stack array -- this function runs on svc_spotify's own task,
 * whose stack this project has deliberately kept modest
 * (SPOTIFY_TASK_STACK, spotify_service.c).
 *
 * The buffer is now caller-owned and allocated once for the service's
 * lifetime (SPOTIFY_RESPONSE_BUF_BYTES, spotify_api.h) rather than
 * heap_caps_malloc()'d and freed on every single call -- see that
 * constant's own comment. The size and the reasoning above are unchanged;
 * only who owns the allocation moved. */

/* Spotify returns pretty-printed JSON, so its error bodies contain real
 * newlines -- logging one raw truncates the line at the first one and
 * throws away the `error.message` field, which is the only part that
 * actually says WHY a call failed (a 404 alone cannot distinguish "no
 * active device" from "device not found" from an unsupported endpoint).
 * Flattened into a caller-owned buffer so the whole message lands on a
 * single log line. */
static void flatten_body(const char *src, size_t src_len, char *dst, size_t dst_cap)
{
    if (dst_cap == 0) {
        return;
    }
    size_t n = (src_len < dst_cap - 1) ? src_len : dst_cap - 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : (char)c;
    }
    dst[n] = '\0';
}

static bool do_request(net_http_t *http, const http_policy_t *policy, const char *url,
                          net_http_method_t method, const char *access_token, bool retryable,
                          net_http_body_t body, char *out_buf, size_t out_cap, net_http_response_t *resp)
{
    net_http_request_opts_t opts = {
        .method               = method,
        .bearer_token         = access_token,
        .body                 = body,
        .retryable_after_send = retryable,
    };
    return net_http_request(http, url, &opts, policy, out_buf, out_cap, resp);
}

spotify_api_result_t spotify_api_get_player_state(net_http_t *http, const http_policy_t *policy,
                                                      const char *access_token,
                                                      char *buf, size_t buf_cap, playback_state_t *out)
{
    if (buf == NULL || buf_cap == 0) {
        return SPOTIFY_API_RESULT_FAILED;
    }

    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE, NET_HTTP_METHOD_GET, access_token, true,
                       (net_http_body_t){ 0 }, buf, buf_cap, &resp)) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code == 204) {
        /* Spotify's own, unambiguous "nothing is playing anywhere". */
        ESP_LOGD(TAG, "player-state: HTTP 204, no active device");
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.len >= buf_cap) {
        ESP_LOGW(TAG, "player-state response filled the %u-byte buffer -- likely truncated",
                  (unsigned)buf_cap);
    }

    spotify_parse_status_t ps = spotify_parse_player_state(buf, resp.len, out);
    if (ps == SPOTIFY_PARSE_NO_ACTIVE_DEVICE) {
        /* IMPORTANT: this is NOT the same as the 204 above, even though both
         * map to the same result. spotify_parse_player_state() also returns
         * NO_ACTIVE_DEVICE for an empty or all-whitespace body -- and
         * net_http_request()'s contract is to truncate silently and still
         * report success, so a short or cut-off read on a 200 response
         * arrives here looking exactly like "nothing is playing".
         *
         * Those two have opposite meanings: one is Spotify telling us the
         * truth, the other is us losing the response. Conflating them makes
         * a flaky read present as a device that repeatedly vanishes and
         * reappears, which is indistinguishable from a genuinely flapping
         * Connect session. Logging the status and byte count is what tells
         * them apart: status=200 with len=0 is our bug, not Spotify's. */
        ESP_LOGW(TAG, "player-state: parsed as NO_ACTIVE_DEVICE from HTTP %d with %u body bytes%s",
                  resp.status_code, (unsigned)resp.len,
                  (resp.len == 0) ? "  <-- EMPTY BODY ON A 2xx: a lost/short read, not Spotify" : "");
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (ps == SPOTIFY_PARSE_MALFORMED) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}

spotify_api_result_t spotify_api_get_devices(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token,
                                                 char *buf, size_t buf_cap, spotify_device_list_t *out)
{
    if (buf == NULL || buf_cap == 0) {
        return SPOTIFY_API_RESULT_FAILED;
    }

    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE "/devices", NET_HTTP_METHOD_GET, access_token, true,
                       (net_http_body_t){ 0 }, buf, buf_cap, &resp)) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        return SPOTIFY_API_RESULT_FAILED;
    }

    spotify_parse_status_t ps = spotify_parse_device_list(buf, resp.len, out);
    if (ps == SPOTIFY_PARSE_MALFORMED) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}

spotify_api_result_t spotify_api_execute_cmd(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, const spotify_api_cmd_t *cmd)
{
    char url[160];
    net_http_method_t method;
    /* PUT play/pause/shuffle/repeat/volume are idempotent -- retryable.
     * POST next/previous are side-effecting -- one attempt only, see
     * net_http_request_opts_t.retryable_after_send's own comment. */
    bool retryable;

    switch (cmd->kind) {
    case SPOTIFY_API_CMD_PLAY:
        snprintf(url, sizeof(url), "%s/play", SPOTIFY_API_BASE);
        method = NET_HTTP_METHOD_PUT;
        retryable = true;
        break;
    case SPOTIFY_API_CMD_PAUSE:
        snprintf(url, sizeof(url), "%s/pause", SPOTIFY_API_BASE);
        method = NET_HTTP_METHOD_PUT;
        retryable = true;
        break;
    case SPOTIFY_API_CMD_NEXT:
        snprintf(url, sizeof(url), "%s/next", SPOTIFY_API_BASE);
        method = NET_HTTP_METHOD_POST;
        retryable = false;
        break;
    case SPOTIFY_API_CMD_PREVIOUS:
        snprintf(url, sizeof(url), "%s/previous", SPOTIFY_API_BASE);
        method = NET_HTTP_METHOD_POST;
        retryable = false;
        break;
    case SPOTIFY_API_CMD_SET_SHUFFLE:
        snprintf(url, sizeof(url), "%s/shuffle?state=%s", SPOTIFY_API_BASE,
                  cmd->u.shuffle ? "true" : "false");
        method = NET_HTTP_METHOD_PUT;
        retryable = true;
        break;
    case SPOTIFY_API_CMD_SET_REPEAT: {
        const char *mode_str = "off";
        if (cmd->u.repeat_mode == SPOTIFY_REPEAT_TRACK) {
            mode_str = "track";
        } else if (cmd->u.repeat_mode == SPOTIFY_REPEAT_CONTEXT) {
            mode_str = "context";
        }
        snprintf(url, sizeof(url), "%s/repeat?state=%s", SPOTIFY_API_BASE, mode_str);
        method = NET_HTTP_METHOD_PUT;
        retryable = true;
        break;
    }
    case SPOTIFY_API_CMD_SET_VOLUME:
        snprintf(url, sizeof(url), "%s/volume?volume_percent=%u", SPOTIFY_API_BASE,
                  (unsigned)cmd->u.volume_pct);
        method = NET_HTTP_METHOD_PUT;
        retryable = true;
        break;
    case SPOTIFY_API_CMD_SEEK:
        snprintf(url, sizeof(url), "%s/seek?position_ms=%u", SPOTIFY_API_BASE,
                  (unsigned)cmd->u.position_ms);
        method = NET_HTTP_METHOD_PUT;
        retryable = true;   /* idempotent -- setting position to X is safe to retry,
                                same reasoning as SET_VOLUME just above */
        break;
    default:
        return SPOTIFY_API_RESULT_FAILED;
    }

    char buf[256];
    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, url, method, access_token, retryable,
                       (net_http_body_t){ 0 }, buf, sizeof(buf), &resp)) {
        ESP_LOGW(TAG, "cmd kind=%d: no response from %s", (int)cmd->kind, url);
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code == 404) {
        /* Spotify's documented "no active device" for a player command,
         * verified against the real body it returns:
         *   {"error":{"status":404,"message":"Player command failed:
         *    No active device found","reason":"NO_ACTIVE_DEVICE"}}
         * spotify_api_transfer_playback() below has always special-cased
         * this; execute_cmd() never did, so a perfectly clear answer was
         * flattened into FAILED and surfaced as "Couldn't reach Spotify".
         * Reporting it accurately is what lets the caller suppress a
         * retry storm and the UI show a truthful message. */
        ESP_LOGD(TAG, "cmd kind=%d: HTTP 404, no active device", (int)cmd->kind);
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        /* This is the one path behind the user-visible "Couldn't reach
         * Spotify" toast (SPOTIFY_API_RESULT_FAILED -> INTENT_ACK_EVT_
         * REJECTED -> view_model_now_playing_apply.c), and until now it
         * threw the status code away and logged nothing at all -- so a real
         * user-facing failure left literally no trace anywhere, which is
         * the opposite of EVENTS_AND_STATE.md §6.4's "the log is the entire
         * debugging story". The distinction matters because these codes
         * mean very different things and only one of them is "couldn't
         * reach": 404 is Spotify's own "no active device", 403 is a
         * restriction violation (e.g. skip with nothing playing, or a
         * non-Premium account), 429 is rate limiting. The body is included
         * because Spotify puts a human-readable `error.message` in it that
         * names the actual reason. */
        char flat[256];
        flatten_body(buf, resp.len, flat, sizeof(flat));
        ESP_LOGW(TAG, "cmd kind=%d rejected: HTTP %d from %s | body: %s",
                  (int)cmd->kind, resp.status_code, url, flat);
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}

/* PUT /v1/me/player (no trailing path segment -- distinct from every other
 * command above, which all target a sub-path) with a JSON body, the one
 * call in this file that needs one. device_id is a Spotify-generated
 * opaque token (spotify_avr_pin_t.avr_device_id, persisted via `spotify
 * set-avr-device`), not user-authored text -- built with a plain snprintf
 * template, no JSON escaping needed, same trust level this codebase's own
 * PKCE code/state handling already assumes. play=true resumes whatever
 * context/track was last active on the target device -- exactly what
 * w_transport's "nothing is playing anywhere, wake the AVR and resume"
 * path needs, without this function or its caller needing to know or
 * remember what that was. */
spotify_api_result_t spotify_api_transfer_playback(net_http_t *http, const http_policy_t *policy,
                                                        const char *access_token, const char *device_id,
                                                        bool play)
{
    char body_json[128];
    int written = snprintf(body_json, sizeof(body_json), "{\"device_ids\":[\"%s\"],\"play\":%s}",
                              device_id, play ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(body_json)) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    net_http_body_t body = {
        .content_type = "application/json",
        .body         = body_json,
        .body_len     = (size_t)written,
    };

    char buf[256];
    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE, NET_HTTP_METHOD_PUT, access_token, true,
                       body, buf, sizeof(buf), &resp)) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code == 404) {
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;   /* the pinned device id itself is gone/unreachable */
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        /* Same silent-failure gap as spotify_api_execute_cmd() above -- this
         * is the path behind a Play tap when nothing is active anywhere
         * (PLAYBACK_INTENT_RESUME_ON_AVR), so it reaches the same toast. */
        char flat[256];
        flatten_body(buf, resp.len, flat, sizeof(flat));
        ESP_LOGW(TAG, "transfer_playback rejected: HTTP %d | body: %s", resp.status_code, flat);
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}
