/*
 * The target-only glue driving the Receiver screen: subscribes TOPIC_DENON/
 * TOPIC_PLAYBACK (signals only, same "go re-read core_state" contract
 * view_model_now_playing_apply.c's own subs use), rebuilds denon_vm_t every
 * tick (view_model_denon.c's pure builder, untouched), pushes it via
 * ui_router_update().
 *
 * Simpler than view_model_now_playing_apply.c on purpose: no pending/ACK
 * reconciliation state machine. Every Denon-facing control on this screen
 * (w_power_row/w_power_glyph, w_volume_slider) already established that
 * Denon feedback is a near-instant telnet echo, not Spotify's poll-confirmed
 * round trip the pending/toast/3s-abandon machinery exists for -- so this
 * glue has nothing to reconcile and screen_denon_ctx_t.intent_submit_raw is
 * cfg->intent_submit directly, never wrapped.
 */
#ifndef VIEW_MODEL_DENON_APPLY_H
#define VIEW_MODEL_DENON_APPLY_H

#include <stdbool.h>

#include "core_eventbus.h"
#include "core_state.h"
#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_state_t          *state;
    core_eventbus_t          *bus;
    ui_router_t                *router;

    eventbus_sub_h    denon_sub;
    event_t                denon_sub_storage[8];
    eventbus_sub_h    playback_sub;
    event_t                playback_sub_storage[4];
} view_model_denon_apply_t;

/* Subscribes both topics. False (caller aborts the whole boot, matching
 * every other app_wiring_create_*() failure path) if either subscription
 * fails. Call once, before the first apply_tick(). */
bool view_model_denon_apply_init(view_model_denon_apply_t *a, core_state_t *state,
                                     core_eventbus_t *bus, ui_router_t *router);

/* Call once per ui_app_task loop iteration, right alongside view_model_now_
 * playing_apply_tick(). Never blocks. Drains both subscriptions, then
 * unconditionally core_state_reads DOMAIN_DENON + DOMAIN_PLAYBACK, rebuilds
 * denon_vm_t, and pushes it -- not gated on "did anything change this
 * tick", matching the same unconditional-rebuild contract Now Playing's own
 * apply_tick() uses. */
void view_model_denon_apply_tick(view_model_denon_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_DENON_APPLY_H */
