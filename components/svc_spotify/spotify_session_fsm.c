#include "spotify_session_fsm.h"

#include <stddef.h>

static spotify_session_fsm_actions_t *actions_of(void *ctx)
{
    spotify_session_fsm_t *sfsm = (spotify_session_fsm_t *)ctx;
    return &sfsm->actions;
}

#define DEFINE_ACTION(name) \
    static void action_##name(void *ctx, uint32_t event, const void *data) \
    { \
        (void)event; (void)data; \
        spotify_session_fsm_actions_t *a = actions_of(ctx); \
        if (a->name != NULL) a->name(a->ctx); \
    }

DEFINE_ACTION(start_pair)
DEFINE_ACTION(start_exchange)
DEFINE_ACTION(enter_authorized_first_time)
DEFINE_ACTION(report_exchange_failed)
DEFINE_ACTION(start_resume)
DEFINE_ACTION(enter_authorized_resumed)
DEFINE_ACTION(enter_reauth_required)
DEFINE_ACTION(report_resume_failed_transient)
DEFINE_ACTION(start_refresh)
DEFINE_ACTION(report_refresh_failed_transient)
DEFINE_ACTION(unpair)

#undef DEFINE_ACTION

static const fsm_transition_t SPOTIFY_SESSION_TRANSITIONS[] = {
    { SPOTIFY_SESSION_FSM_STATE_UNPAIRED,        SPOTIFY_SESSION_EVT_PAIR_REQUESTED,  action_start_pair,     SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE },
    { SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE,   SPOTIFY_SESSION_EVT_CODE_PASTED,     action_start_exchange, SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE },
    { SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE, SPOTIFY_SESSION_EVT_EXCHANGE_OK,     action_enter_authorized_first_time, SPOTIFY_SESSION_FSM_STATE_AUTHORIZED },
    /* Single-use code -- whatever failed, the user needs a fresh `spotify
     * pair` for a new code, not a retry of this one. */
    { SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE, SPOTIFY_SESSION_EVT_EXCHANGE_FAILED, action_report_exchange_failed, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },

    /* Boot-time resume: self-transition while the blocking refresh-token
     * call runs (denon_service_link.c's own discipline -- the action can't
     * safely dispatch the real follow-up itself). */
    { SPOTIFY_SESSION_FSM_STATE_RESUMING, SPOTIFY_SESSION_EVT_RESUME_DUE,              action_start_resume,                  SPOTIFY_SESSION_FSM_STATE_RESUMING },
    { SPOTIFY_SESSION_FSM_STATE_RESUMING, SPOTIFY_SESSION_EVT_RESUME_OK,               action_enter_authorized_resumed,      SPOTIFY_SESSION_FSM_STATE_AUTHORIZED },
    { SPOTIFY_SESSION_FSM_STATE_RESUMING, SPOTIFY_SESSION_EVT_RESUME_FAILED_AUTH,      action_enter_reauth_required,         SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED },
    { SPOTIFY_SESSION_FSM_STATE_RESUMING, SPOTIFY_SESSION_EVT_RESUME_FAILED_TRANSIENT, action_report_resume_failed_transient, SPOTIFY_SESSION_FSM_STATE_RESUMING },

    /* Mid-session proactive refresh (80% of expires_in). Transient failure
     * returns to AUTHORIZED, not REFRESHING -- the old access token is
     * still usable a while longer; only a definitive invalid_grant forces
     * REAUTH_REQUIRED. */
    { SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, SPOTIFY_SESSION_EVT_REFRESH_DUE,              action_start_refresh,                  SPOTIFY_SESSION_FSM_STATE_REFRESHING },
    { SPOTIFY_SESSION_FSM_STATE_REFRESHING, SPOTIFY_SESSION_EVT_REFRESH_OK,               action_enter_authorized_resumed,       SPOTIFY_SESSION_FSM_STATE_AUTHORIZED },
    { SPOTIFY_SESSION_FSM_STATE_REFRESHING, SPOTIFY_SESSION_EVT_REFRESH_FAILED_AUTH,      action_enter_reauth_required,          SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED },
    { SPOTIFY_SESSION_FSM_STATE_REFRESHING, SPOTIFY_SESSION_EVT_REFRESH_FAILED_TRANSIENT, action_report_refresh_failed_transient, SPOTIFY_SESSION_FSM_STATE_AUTHORIZED },

    /* Re-pairing directly from REAUTH_REQUIRED -- no forced explicit unpair
     * step first, matching ordinary OAuth reauth UX ("connect" again). */
    { SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, action_start_pair, SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE },

    /* `spotify unpair` -- a hard reset, valid from every real state. */
    { SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE,   SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
    { SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE, SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
    { SPOTIFY_SESSION_FSM_STATE_RESUMING,        SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
    { SPOTIFY_SESSION_FSM_STATE_AUTHORIZED,      SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
    { SPOTIFY_SESSION_FSM_STATE_REFRESHING,      SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
    { SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED, SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, action_unpair, SPOTIFY_SESSION_FSM_STATE_UNPAIRED },
};

static const fsm_table_t SPOTIFY_SESSION_TABLE = {
    .transitions = SPOTIFY_SESSION_TRANSITIONS,
    .count       = sizeof(SPOTIFY_SESSION_TRANSITIONS) / sizeof(SPOTIFY_SESSION_TRANSITIONS[0]),
};

void spotify_session_fsm_init(spotify_session_fsm_t *sfsm, spotify_session_fsm_state_t initial_state,
                                 const spotify_session_fsm_actions_t *actions,
                                 fsm_transition_log_fn on_transition, void *log_ctx)
{
    sfsm->actions = *actions;

    fsm_cfg_t cfg = {
        .name          = "spotify_session",
        .table         = &SPOTIFY_SESSION_TABLE,
        .initial_state = initial_state,
        .on_transition = on_transition,
        .log_ctx       = log_ctx,
        .action_ctx    = sfsm,
    };
    core_fsm_init(&sfsm->fsm, &cfg);
}

fsm_result_t spotify_session_fsm_dispatch(spotify_session_fsm_t *sfsm, spotify_session_fsm_event_t event,
                                             const void *event_data)
{
    return core_fsm_dispatch(&sfsm->fsm, event, event_data);
}

spotify_session_fsm_state_t spotify_session_fsm_current_state(const spotify_session_fsm_t *sfsm)
{
    return (spotify_session_fsm_state_t)core_fsm_current_state(&sfsm->fsm);
}
