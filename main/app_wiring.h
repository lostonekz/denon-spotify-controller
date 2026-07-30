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

#endif /* APP_WIRING_H */
