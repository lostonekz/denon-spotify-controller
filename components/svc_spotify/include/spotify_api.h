/*
 * api.spotify.com calls: GET player state (handling 204 "nothing playing
 * anywhere"), GET devices, and executing a translated playback command
 * (spotify_intent.h's spotify_api_cmd_t). Every call carries the bearer
 * access token -- the token endpoint (spotify_auth.h) is the only Spotify
 * call that doesn't. Target-only glue around spotify_parse.c/net_http.c;
 * verified by idf.py build + Tier 6 hardware, same as spotify_auth.c.
 */
#ifndef SPOTIFY_API_H
#define SPOTIFY_API_H

#include "net_http.h"
#include "spotify_intent.h"
#include "spotify_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPOTIFY_API_RESULT_OK = 0,
    SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE,   /* 204 -- valid, not an error */
    SPOTIFY_API_RESULT_UNAUTHORIZED,       /* 401 -- access token expired/invalid; caller should refresh */
    SPOTIFY_API_RESULT_FAILED,
} spotify_api_result_t;

spotify_api_result_t spotify_api_get_player_state(net_http_t *http, const http_policy_t *policy,
                                                      const char *access_token, playback_state_t *out);

spotify_api_result_t spotify_api_get_devices(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, spotify_device_list_t *out);

spotify_api_result_t spotify_api_execute_cmd(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, const spotify_api_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_API_H */
