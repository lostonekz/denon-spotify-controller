/*
 * The Spotify session FSM. docs/EVENTS_AND_STATE.md §6.4 lists
 * spotify_session among the FSMs that must be table-driven on core_fsm_t.
 * Built exactly like denon_link_fsm.c/ota_fsm.c: side effects injected via
 * spotify_session_fsm_actions_t, host tests supply call-counting fakes and
 * assert the exact transition sequence.
 *
 * Initial state is caller-chosen at construction (same shape as
 * ota_fsm_init/denon_link_fsm_init): RESUMING if spotify_credentials_load()
 * finds a stored refresh token, UNPAIRED otherwise.
 *
 * RESUMING is split from REFRESHING even though both ultimately call the
 * same token-refresh endpoint: a boot-time resume (no usable access token
 * yet) and a mid-session proactive refresh (still holding a valid access
 * token) need different transient-failure handling --
 * RESUME_FAILED_TRANSIENT stays in RESUMING (retry with backoff, nothing
 * usable yet) while REFRESH_FAILED_TRANSIENT returns to AUTHORIZED (keep
 * using the still-valid token, retry sooner). core_fsm_t is purely
 * table-driven, so one (state,event) row can't pick a different next_state
 * based on how the attempt was entered -- splitting the state is the
 * honest way to keep that decision in the table.
 *
 * EXCHANGE_FAILED (whichever way it failed) always returns to UNPAIRED,
 * not back to AWAITING_CODE: the PKCE authorization code is single-use, so
 * whatever failed, the user needs a fresh `spotify pair` for a new code
 * regardless of cause -- there is no "retry the same code" path.
 *
 * *_FAILED_AUTH (a real "error":"invalid_grant" response, never a retry
 * count) is the only path to REAUTH_REQUIRED -- docs/RISKS.md R4 frames
 * that state as unrecoverable auth failure (revoked, password changed),
 * not "we've been unlucky." *_FAILED_TRANSIENT (timeout/5xx) never
 * auto-escalates to REAUTH_REQUIRED from repetition alone.
 *
 * Actions perform their real work (blocking HTTP calls) synchronously and
 * store the result in a "just_ran"/"ok"/"auth_failed" flag pair on the
 * caller's own state (mirrors denon_service_link.c's identical discipline,
 * for the identical reason: core_fsm_dispatch() runs an action BEFORE
 * updating current_state, so an action cannot safely dispatch back into
 * the FSM itself -- the caller's own drive loop, called every task
 * iteration, notices a set flag and dispatches the real follow-up event
 * from OUTSIDE any action).
 */
#ifndef SPOTIFY_SESSION_FSM_H
#define SPOTIFY_SESSION_FSM_H

#include <stdint.h>

#include "core_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPOTIFY_SESSION_FSM_STATE_UNPAIRED = 0,
    SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE,
    SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE,
    SPOTIFY_SESSION_FSM_STATE_RESUMING,
    SPOTIFY_SESSION_FSM_STATE_AUTHORIZED,
    SPOTIFY_SESSION_FSM_STATE_REFRESHING,
    SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED,
} spotify_session_fsm_state_t;

typedef enum {
    SPOTIFY_SESSION_EVT_PAIR_REQUESTED = 0,
    SPOTIFY_SESSION_EVT_CODE_PASTED,
    SPOTIFY_SESSION_EVT_EXCHANGE_OK,
    SPOTIFY_SESSION_EVT_EXCHANGE_FAILED,
    SPOTIFY_SESSION_EVT_RESUME_DUE,
    SPOTIFY_SESSION_EVT_RESUME_OK,
    SPOTIFY_SESSION_EVT_RESUME_FAILED_AUTH,
    SPOTIFY_SESSION_EVT_RESUME_FAILED_TRANSIENT,
    SPOTIFY_SESSION_EVT_REFRESH_DUE,
    SPOTIFY_SESSION_EVT_REFRESH_OK,
    SPOTIFY_SESSION_EVT_REFRESH_FAILED_AUTH,
    SPOTIFY_SESSION_EVT_REFRESH_FAILED_TRANSIENT,
    SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED,
} spotify_session_fsm_event_t;

typedef struct {
    void (*start_pair)(void *ctx);
    void (*start_exchange)(void *ctx);
    void (*enter_authorized_first_time)(void *ctx);   /* EXCHANGE_OK -- persists paired_at_epoch_s */
    void (*report_exchange_failed)(void *ctx);
    void (*start_resume)(void *ctx);
    void (*enter_authorized_resumed)(void *ctx);      /* RESUME_OK or REFRESH_OK -- paired_at untouched */
    void (*enter_reauth_required)(void *ctx);          /* RESUME_FAILED_AUTH or REFRESH_FAILED_AUTH */
    void (*report_resume_failed_transient)(void *ctx);
    void (*start_refresh)(void *ctx);
    void (*report_refresh_failed_transient)(void *ctx);
    void (*unpair)(void *ctx);
    void *ctx;
} spotify_session_fsm_actions_t;

typedef struct {
    core_fsm_t                      fsm;
    spotify_session_fsm_actions_t     actions;
} spotify_session_fsm_t;

void spotify_session_fsm_init(spotify_session_fsm_t *sfsm, spotify_session_fsm_state_t initial_state,
                                 const spotify_session_fsm_actions_t *actions,
                                 fsm_transition_log_fn on_transition, void *log_ctx);

fsm_result_t spotify_session_fsm_dispatch(spotify_session_fsm_t *sfsm, spotify_session_fsm_event_t event,
                                             const void *event_data);

spotify_session_fsm_state_t spotify_session_fsm_current_state(const spotify_session_fsm_t *sfsm);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_SESSION_FSM_H */
