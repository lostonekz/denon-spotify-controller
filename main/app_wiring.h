/*
 * Composition root. Constructs every Phase 1 handle in dependency order,
 * wires the boot graph, and starts the periodic ticks. docs/adr/0002.
 */
#ifndef APP_WIRING_H
#define APP_WIRING_H

#include <stdbool.h>

#include "app_ctx.h"

/* False on any construction or boot-graph failure -- app_start.c decides
 * what to do about it. ctx is left partially constructed on failure; the
 * caller is expected to reboot, not to keep using it. */
bool app_wiring_create(app_ctx_t *ctx);

/* EVENTS_AND_STATE.md §5.1's single intent queue -- app_wiring's
 * intent_dispatch_tick_trampoline drains it and routes by
 * intent_domain_t. Thread-safe (a plain FreeRTOS queue); the debug
 * console (docs/ROADMAP.md Phase 3 Tier 5) is the first real caller.
 * False (and ctx->intent_drop_count incremented) if the queue is full --
 * drop newest, not block the caller or evict an already-queued intent. */
bool app_wiring_submit_intent(app_ctx_t *ctx, const intent_t *intent);

#endif /* APP_WIRING_H */
