/*
 * See spotify_service_state.h. Pure: no ESP-IDF, no I/O, no allocation.
 * Host-tested (test/host/test_spotify_service_state.c).
 */
#include "spotify_service_state.h"

#include <string.h>

/* strncpy() does not null-terminate when src is exactly dst_cap-1 bytes or
 * longer -- guaranteed to happen for Spotify's client_id, a fixed 32-hex-char
 * string that exactly fills SPOTIFY_CLIENT_ID_MAX-1. Safe here today only
 * because memset() below already zeroed the whole struct first; fixed
 * explicitly anyway so this doesn't silently break if that ordering ever
 * changes (found the hard way in the impure copies this pure init feeds --
 * see spotify_service.c's copy of this same helper). */
static void safe_strncpy(char *dst, const char *src, size_t dst_cap)
{
    if (dst_cap == 0) {
        return;
    }
    strncpy(dst, src, dst_cap - 1);
    dst[dst_cap - 1] = '\0';
}

bool spotify_service_state_init(spotify_service_state_t *s, const spotify_service_state_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));

    safe_strncpy(s->client_id, cfg->client_id, sizeof(s->client_id));
    safe_strncpy(s->redirect_uri, cfg->redirect_uri, sizeof(s->redirect_uri));
    s->avr_pin = cfg->stored_avr_pin;
    s->force_poll_index = -1;

    /* Caller-chosen initial state (ota_fsm-style, not net_wifi_fsm's fixed
     * BOOT): RESUMING with resume_pending set when a stored refresh token
     * exists, so the drive loop's first iteration actually kicks off a
     * real resume attempt -- exactly the Tier -1 boot-kick fix applied to
     * this FSM from the start, rather than discovered later the way
     * Denon's cached-host path was. */
    spotify_session_fsm_state_t initial = SPOTIFY_SESSION_FSM_STATE_UNPAIRED;
    if (cfg->has_stored_creds) {
        safe_strncpy(s->refresh_token, cfg->stored_creds.refresh_token, sizeof(s->refresh_token));
        s->paired_at_epoch_s = cfg->stored_creds.paired_at_epoch_s;
        initial              = SPOTIFY_SESSION_FSM_STATE_RESUMING;
        s->resume_pending      = true;
    }

    /* All-NULL actions -- inert, no I/O possible. The real vtable is
     * installed by svc_spotify_create() via a direct field assignment on
     * s->session_fsm.actions, not by re-calling spotify_session_fsm_init(),
     * which would reset current_state -- same discipline as
     * denon_service_state_init(). */
    spotify_session_fsm_actions_t noop_actions = { 0 };
    spotify_session_fsm_init(&s->session_fsm, initial, &noop_actions,
                                cfg->session_fsm_log, cfg->session_fsm_log_ctx);

    return true;
}

void spotify_service_state_reset(spotify_service_state_t *s)
{
    memset(s, 0, sizeof(*s));
}
