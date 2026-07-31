/*
 * The software state that must correctly reset across a svc_spotify
 * restart: the session FSM, cached credentials/config, the AVR pin, and
 * the drive-loop/poll-scheduling bookkeeping. Deliberately does NOT
 * include anything that owns an OS or network resource (the ctrl/intent
 * queues, session_lock, the spotify task, http/time/bus/state_store
 * pointers) -- those live in svc_spotify_t (spotify_service.h) instead.
 *
 * WHY THE SPLIT: identical reasoning to denon_service_state.h --
 * svc_spotify_t necessarily embeds a net_http_t pointer and a net_time_t
 * pointer (both anonymous-struct typedefs around real FreeRTOS handles),
 * so anything that references it pulls in headers test/host's harness
 * cannot see. This file is the piece that CAN be -- and is -- host-tested
 * (test/host/test_spotify_service_state.c), capturing exactly the
 * property a restartability test cares about: does every pure field
 * return to the same baseline across repeated init->reset->init cycles.
 *
 * Includes spotify_auth_types.h, NOT spotify_auth.h -- the latter pulls in
 * net_http.h for its own function signatures, which this file must never
 * transitively include.
 */
#ifndef SPOTIFY_SERVICE_STATE_H
#define SPOTIFY_SERVICE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "core_fsm.h"

#include "spotify_auth_types.h"
#include "spotify_credentials.h"
#include "spotify_parse.h"
#include "spotify_poll_policy.h"
#include "spotify_session_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_CTRL_CMD_ARG_MAX 600   /* CODE_PASTED needs the full pasted redirect URL */

typedef struct {
    char client_id[SPOTIFY_CLIENT_ID_MAX];        /* resolved snapshot -- app_wiring's Kconfig
                                                     * seeding already happened before this */
    char redirect_uri[SPOTIFY_REDIRECT_URI_MAX];

    /* From spotify_credentials_load(); all-zero/false if nothing was
     * stored yet (first boot, never paired). */
    bool                            has_stored_creds;
    spotify_session_credentials_t     stored_creds;
    spotify_avr_pin_t                    stored_avr_pin;   /* from spotify_avr_pin_load(); may be unset */

    /* Nullable, matches core_fsm's own contract -- NULL in host tests, a
     * real ESP_LOGI-based logger from app_wiring in the target build. */
    fsm_transition_log_fn session_fsm_log;
    void                    *session_fsm_log_ctx;
} spotify_service_state_cfg_t;

typedef struct {
    spotify_session_fsm_t session_fsm;

    char    client_id[SPOTIFY_CLIENT_ID_MAX];
    char    redirect_uri[SPOTIFY_REDIRECT_URI_MAX];
    char    refresh_token[SPOTIFY_REFRESH_TOKEN_MAX];
    int64_t paired_at_epoch_s;

    spotify_avr_pin_t            avr_pin;
    spotify_auth_pending_pair_t    pending_pair;
    char                              pending_state[SPOTIFY_STATE_NONCE_MAX];
    spotify_device_list_t               last_devices;
    spotify_session_summary_t              session_status;
    bool                                      reauth_recommended;

    char     access_token[SPOTIFY_ACCESS_TOKEN_MAX];
    int64_t  access_token_refresh_due_us;
    int64_t  resume_backoff_deadline_us;
    char     pending_code[SPOTIFY_CTRL_CMD_ARG_MAX];
    uint32_t consecutive_poll_errors;
    int64_t  next_poll_due_us;
    int64_t  force_poll_deadline_us[SPOTIFY_POLL_FORCE_POLL_COUNT];
    int      force_poll_index;
    bool     resume_pending;

    /* Cross-boundary action-result flags -- see spotify_service.h's top
     * comment for why (mirrors denon_service_link.c's own discipline). */
    bool                     token_op_just_ran;
    spotify_auth_result_t      token_op_result;
    spotify_token_response_t     token_op_tokens;
} spotify_service_state_t;

bool spotify_service_state_init(spotify_service_state_t *s, const spotify_service_state_cfg_t *cfg);
void spotify_service_state_reset(spotify_service_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_SERVICE_STATE_H */
