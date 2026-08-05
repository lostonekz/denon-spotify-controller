/*
 * intent_t.kind/payload when intent_t.domain == INTENT_DOMAIN_PAIRING.
 *
 * Submitted by the Spotify pair overlay's own mount()/unmount() -- the first
 * screen in this project whose lifecycle hooks do real work rather than being
 * (void)ctx; no-ops. Handled in main/app_wiring_ticks.c, which starts/stops
 * the pairing HTTP listener (main/pair_http.c).
 *
 * Lives in core_types for the same reason settings_intent_types.h does: `ui`
 * may not include anything from the svc or app layers (tools/arch_rules.json),
 * so the intent's shape has to sit somewhere both `ui` and `main` can reach.
 *
 * Deliberately NOT ridden in on an existing domain. INTENT_DOMAIN_SETTINGS's
 * dispatch case means "write a typed value through svc_settings", and
 * PLATFORM.md §3.4 keeps Spotify auth out of typed settings entirely.
 * INTENT_DOMAIN_PLAYBACK routes into spotify_intent_to_cmd(), which would
 * reject an unknown kind and raise a REJECTED ack and a user-visible toast.
 */
#ifndef CORE_PAIRING_INTENT_TYPES_H
#define CORE_PAIRING_INTENT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAIRING_INTENT_START = 0,   /* overlay presented, or the user confirmed a forced re-pair */
    PAIRING_INTENT_STOP,        /* overlay dismissed -- the listener must not outlive it */
} pairing_intent_kind_t;

typedef struct {
    /* 0: only begin a pairing exchange when the session actually needs one
     *    (UNPAIRED / REAUTH_REQUIRED). A healthy session is left alone.
     * 1: the user explicitly asked to reconnect a different account, so
     *    unpair first. Needed because spotify_session_fsm accepts
     *    PAIR_REQUESTED only from UNPAIRED/REAUTH_REQUIRED -- from AUTHORIZED
     *    it is a silent no-op, and so is the code paste that follows it. */
    uint8_t force;
} pairing_intent_payload_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_PAIRING_INTENT_TYPES_H */
