/*
 * The target-only glue UI_ARCHITECTURE.md §2's apply_pending_vm_updates()
 * step drives: subscribes TOPIC_PLAYBACK/ARTWORK/DENON/INTENT_ACK/TOAST,
 * rebuilds now_playing_vm_t every tick (view_model_now_playing.c's pure
 * builder, Tier 1, untouched), and pushes it via ui_router_update().
 *
 * Deliberately a separate file from view_model_now_playing.c, not more
 * code added to it -- that file is pure/host-tested (test_view_model_now_
 * playing.c links only it + its pure dependencies, zero LVGL/ESP-IDF);
 * eventbus subscription and the pending/ACK-reconciliation state machine
 * below are target-only by nature (they touch core_eventbus_t, a live
 * subscription, real wall-clock timing) and would have entangled that
 * host-testability if folded into the same translation unit. Same split
 * this codebase already uses elsewhere (e.g. denon_service_state.c pure
 * vs. denon_service.c target).
 *
 * Also owns the "intent submit provider ui_app hands to the screen" --
 * screen_now_playing_ctx_t.intent_submit must be THIS module's own wrapped
 * provider (view_model_now_playing_apply_intent_submit_provider()), not
 * the raw one main/app_wiring_ui.c constructs, or pending/ACK
 * reconciliation never actually engages: this is the one place that
 * observes every submit() call to start tracking it.
 */
#ifndef VIEW_MODEL_NOW_PLAYING_APPLY_H
#define VIEW_MODEL_NOW_PLAYING_APPLY_H

#include <stdbool.h>
#include <stdint.h>

#include "core_eventbus.h"
#include "core_state.h"
#include "intent_submit_types.h"
#include "os_if_types.h"
#include "screen_now_playing.h"
#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_state_t                 *state;
    core_eventbus_t                 *bus;
    ui_router_t                       *router;
    screen_now_playing_ctx_t             *screen_ctx;   /* for w_toast_show() on a real reject */
    intent_submit_provider_t                real_submit;   /* the actual injected provider --
                                                                never handed to the screen directly */
    time_source_t                              clock;

    eventbus_sub_h    playback_sub;
    event_t                playback_sub_storage[4];
    eventbus_sub_h    artwork_sub;
    event_t                artwork_sub_storage[2];
    eventbus_sub_h    denon_sub;
    event_t                denon_sub_storage[8];
    eventbus_sub_h    intent_ack_sub;
    event_t                intent_ack_sub_storage[4];
    eventbus_sub_h    toast_sub;
    event_t                toast_sub_storage[4];

    bool       pending;
    uint32_t     pending_seq;
    int64_t        pending_since_us;
} view_model_now_playing_apply_t;

/* Subscribes to all 5 topics. False (nothing subscribed that failed is
 * torn back down -- caller aborts the whole boot on a false return,
 * matching every other app_wiring_create_*() failure path) if any
 * subscription fails. Call once, before the first apply_tick(). */
bool view_model_now_playing_apply_init(view_model_now_playing_apply_t *a, core_state_t *state,
                                          core_eventbus_t *bus, ui_router_t *router,
                                          screen_now_playing_ctx_t *screen_ctx,
                                          intent_submit_provider_t real_submit, time_source_t clock);

/* Call once per ui_app_task loop iteration (UI_ARCHITECTURE.md §2's
 * apply_pending_vm_updates() step). Never blocks: eventbus polling is
 * always non-blocking, core_state_read() is a bounded mutex-guarded
 * memcpy. Drains all 5 subscriptions, reconciles pending against any
 * matching TOPIC_INTENT_ACK (toast + clear on REJECTED, silent clear on
 * ACCEPTED, clear after a 3s unconfirmed timeout per EVENTS_AND_STATE.md
 * §6.3 step 5), shows any TOPIC_TOAST message directly, then
 * unconditionally rebuilds and pushes the vm -- not gated on "did
 * anything change this tick", because the progress bar must keep
 * interpolating smoothly every tick while playing (UI_ARCHITECTURE.md §6:
 * "never a visible jump"), which by definition isn't a discrete "change". */
void view_model_now_playing_apply_tick(view_model_now_playing_apply_t *a);

/* The provider to hand to screen_now_playing_ctx_t.intent_submit (and, in
 * turn, to w_transport) -- wraps real_submit so every successful submit()
 * starts this glue's own pending/ACK tracking. Safe to call before init()
 * has run (the returned provider only reads `a`'s fields at call time, on
 * an actual tap, by which point init() will already have completed). */
intent_submit_provider_t view_model_now_playing_apply_intent_submit_provider(view_model_now_playing_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_NOW_PLAYING_APPLY_H */
