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
#include "spotify_api_result.h"   /* spotify_api_result_t -- split out in Phase 5 Tier -1
                                     * so spotify_intent_ack.h can stay pure/host-testable */
#include "spotify_intent.h"
#include "spotify_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

spotify_api_result_t spotify_api_get_player_state(net_http_t *http, const http_policy_t *policy,
                                                      const char *access_token, playback_state_t *out);

spotify_api_result_t spotify_api_get_devices(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, spotify_device_list_t *out);

spotify_api_result_t spotify_api_execute_cmd(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token, const spotify_api_cmd_t *cmd);

/* PUT /v1/me/player {"device_ids":[device_id],"play":play} -- transfers
 * playback to device_id; play=true resumes whatever context/track was
 * last active there. Used by svc_spotify's own PLAYBACK_INTENT_RESUME_ON_AVR
 * handling (spotify_service_task.c), not reachable through
 * spotify_intent_to_cmd()/spotify_api_cmd_t -- that translator is pure and
 * has no access to the persisted AVR device-id pin this needs. */
spotify_api_result_t spotify_api_transfer_playback(net_http_t *http, const http_policy_t *policy,
                                                        const char *access_token, const char *device_id,
                                                        bool play);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_API_H */
