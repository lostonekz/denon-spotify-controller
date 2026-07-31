/*
 * The volume-retargeting coordinator's wiring: reads a consistent snapshot
 * of all three domains volume_coordinator.h's pure resolve_volume_target()
 * needs, calls it, and repacks the result into a real single-domain
 * intent_t. Split out of app_wiring_ticks.c purely for the 400-line gate.
 *
 * Deliberately returns the resolved intent rather than submitting it
 * directly -- intent_dispatch_tick_trampoline's own dispatch switch
 * (app_wiring_ticks.c) calls itself recursively with the result, so there
 * is exactly one place that ever calls svc_denon_submit_intent()/
 * svc_spotify_submit_intent(), not a second bypass path.
 */
#ifndef APP_WIRING_VOLUME_H
#define APP_WIRING_VOLUME_H

#include "app_ctx.h"
#include "volume_coordinator.h"

/* Returns an intent_t with .domain == INTENT_DOMAIN_COUNT (an invalid
 * sentinel, not a real domain) if `intent` wasn't a recognized MULTI kind
 * -- the caller drops it rather than recursing on garbage. */
intent_t app_wiring_resolve_multi_intent(app_ctx_t *ctx, const intent_t *intent);

#endif /* APP_WIRING_VOLUME_H */
