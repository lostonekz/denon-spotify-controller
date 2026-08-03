#include "spotify_api.h"

#include <stdio.h>

#include "esp_heap_caps.h"
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
 * parsed response) while title/artist/artwork never appeared. PSRAM-
 * backed heap allocation, not a bigger stack array -- this function runs
 * on svc_spotify's own task, whose stack this project has deliberately
 * kept modest (SPOTIFY_TASK_STACK, spotify_service.c). */
#define SPOTIFY_PLAYER_STATE_BUF_BYTES 16384
#define SPOTIFY_DEVICES_BUF_BYTES       6144

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
                                                      const char *access_token, playback_state_t *out)
{
    char *buf = heap_caps_malloc(SPOTIFY_PLAYER_STATE_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGW(TAG, "player-state buffer alloc failed");
        return SPOTIFY_API_RESULT_FAILED;
    }

    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE, NET_HTTP_METHOD_GET, access_token, true,
                       (net_http_body_t){ 0 }, buf, SPOTIFY_PLAYER_STATE_BUF_BYTES, &resp)) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code == 204) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.len >= SPOTIFY_PLAYER_STATE_BUF_BYTES) {
        ESP_LOGW(TAG, "player-state response filled the %d-byte buffer -- likely truncated",
                  SPOTIFY_PLAYER_STATE_BUF_BYTES);
    }

    spotify_parse_status_t ps = spotify_parse_player_state(buf, resp.len, out);
    heap_caps_free(buf);
    if (ps == SPOTIFY_PARSE_NO_ACTIVE_DEVICE) {
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (ps == SPOTIFY_PARSE_MALFORMED) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}

spotify_api_result_t spotify_api_get_devices(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, spotify_device_list_t *out)
{
    char *buf = heap_caps_malloc(SPOTIFY_DEVICES_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGW(TAG, "device-list buffer alloc failed");
        return SPOTIFY_API_RESULT_FAILED;
    }

    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE "/devices", NET_HTTP_METHOD_GET, access_token, true,
                       (net_http_body_t){ 0 }, buf, SPOTIFY_DEVICES_BUF_BYTES, &resp)) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        heap_caps_free(buf);
        return SPOTIFY_API_RESULT_FAILED;
    }

    spotify_parse_status_t ps = spotify_parse_device_list(buf, resp.len, out);
    heap_caps_free(buf);
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
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
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
        return SPOTIFY_API_RESULT_FAILED;
    }
    return SPOTIFY_API_RESULT_OK;
}
