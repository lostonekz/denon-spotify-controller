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

/* SPOTIFY_RESPONSE_BUF_BYTES: the size a caller must provide for the two
 * response-parsing calls below. Sized for /v1/me/player, the larger of the
 * two (a real response with a full track + artists[] + album images[],
 * device object, context and actions routinely exceeds 4096 -- see
 * spotify_api.c's own comment on the real truncation bug that measured
 * this). The device-list response is much smaller and deliberately shares
 * the same buffer rather than justifying a second one.
 *
 * The buffer is caller-owned, not allocated per call: both of these run on
 * svc_spotify's own task, sequentially and never concurrently (the poll
 * from spotify_service_tick(), the device list from a ctrl command drained
 * at the top of that same function), so one buffer for the service's
 * lifetime is enough. Previously each call heap_caps_malloc()'d and freed
 * 16 KiB of PSRAM -- on the poll path that is a real allocate/free pair
 * every 5 seconds, forever, purely to hold a response for the few
 * milliseconds it takes to parse. Passing it in also keeps these functions
 * free of any allocation policy of their own, matching how every other
 * buffer in this project is owned by a named domain (MEMORY.md §2). */
#define SPOTIFY_RESPONSE_BUF_BYTES 16384

spotify_api_result_t spotify_api_get_player_state(net_http_t *http, const http_policy_t *policy,
                                                      const char *access_token,
                                                      char *buf, size_t buf_cap, playback_state_t *out);

spotify_api_result_t spotify_api_get_devices(net_http_t *http, const http_policy_t *policy,
                                                 const char *access_token,
                                                 char *buf, size_t buf_cap, spotify_device_list_t *out);

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
