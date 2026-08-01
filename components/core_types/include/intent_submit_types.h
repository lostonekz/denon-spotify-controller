/*
 * The injected capability `ui` uses to submit an intent -- Phase 5 Tier 6.
 *
 * docs/adr/0001-intents-down-state-up.md: "The UI posts intent_t values to
 * a single intent queue. It never calls a service function." True in
 * principle, but the real queue entry point, app_wiring_submit_intent(app_
 * ctx_t*, const intent_t*), takes app_ctx_t -- layer "app", forbidden to
 * `ui` (tools/arch_rules.json: ui -> [core, lvgl, idf, ui_hal_glue] only).
 * Same shape as artwork_pixel_types.h's artwork_pixel_provider_t, the one
 * existing precedent for crossing exactly this boundary: a small POD
 * function-pointer struct living in core_types (legal for `ui`), with the
 * real implementation supplied by main/app_wiring_ui.c and threaded into
 * ui_app_cfg_t the same way artwork_pixels already is.
 *
 * submit() takes a non-const intent_t* so the real implementation can stamp
 * intent->seq with a freshly-assigned value before enqueueing -- no
 * existing caller (debug_console_spotify_playback.c, app_wiring_volume.c)
 * ever set `seq` themselves (confirmed by reading both: every intent_t is
 * `{0}`-initialized and never touches `.seq`), because none of them needed
 * to watch for the resulting TOPIC_INTENT_ACK. `ui`'s optimistic-intent
 * pending/reconciliation flow (EVENTS_AND_STATE.md §6.3) is the first real
 * caller that does, so seq assignment has to start being real somewhere --
 * submit()'s own implementation is that place, and the caller reads
 * intent->seq back out after a successful call to know which seq to watch
 * for.
 */
#ifndef CORE_INTENT_SUBMIT_TYPES_H
#define CORE_INTENT_SUBMIT_TYPES_H

#include <stdbool.h>

#include "intent_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    /* False if the intent queue is full (drop, matching app_wiring_submit_
     * intent()'s own contract) -- intent->seq is stamped regardless of the
     * return value's meaning to the caller, but only a true return means
     * anything was actually enqueued to eventually produce a real ACK. */
    bool (*submit)(void *ctx, intent_t *intent);
} intent_submit_provider_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_INTENT_SUBMIT_TYPES_H */
