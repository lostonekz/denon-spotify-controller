#include "spotify_api.h"

#include <stdio.h>

#define SPOTIFY_API_BASE "https://api.spotify.com/v1/me/player"

static bool do_request(net_http_t *http, const http_policy_t *policy, const char *url,
                          net_http_method_t method, const char *access_token, bool retryable,
                          char *out_buf, size_t out_cap, net_http_response_t *resp)
{
    net_http_request_opts_t opts = {
        .method               = method,
        .bearer_token         = access_token,
        .body                 = { 0 },
        .retryable_after_send = retryable,
    };
    return net_http_request(http, url, &opts, policy, out_buf, out_cap, resp);
}

spotify_api_result_t spotify_api_get_player_state(net_http_t *http, const http_policy_t *policy,
                                                      const char *access_token, playback_state_t *out)
{
    char buf[4096];
    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE, NET_HTTP_METHOD_GET, access_token, true,
                       buf, sizeof(buf), &resp)) {
        return SPOTIFY_API_RESULT_FAILED;
    }
    if (resp.status_code == 401) {
        return SPOTIFY_API_RESULT_UNAUTHORIZED;
    }
    if (resp.status_code == 204) {
        return SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        return SPOTIFY_API_RESULT_FAILED;
    }

    spotify_parse_status_t ps = spotify_parse_player_state(buf, resp.len, out);
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
    char buf[2048];
    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, SPOTIFY_API_BASE "/devices", NET_HTTP_METHOD_GET, access_token, true,
                       buf, sizeof(buf), &resp)) {
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
    default:
        return SPOTIFY_API_RESULT_FAILED;
    }

    char buf[256];
    net_http_response_t resp = { 0 };
    if (!do_request(http, policy, url, method, access_token, retryable, buf, sizeof(buf), &resp)) {
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
