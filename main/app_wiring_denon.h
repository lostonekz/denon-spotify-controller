/*
 * svc_denon construction (reads the initial settings snapshot, builds
 * svc_denon_cfg_t, calls svc_denon_create(), retains the restart ctx).
 * Split out of app_wiring.c purely for the 400-line gate (check_limits.py)
 * -- still part of the composition root, not a reusable component; not
 * installed, no other file includes this.
 */
#ifndef APP_WIRING_DENON_H
#define APP_WIRING_DENON_H

#include <stdbool.h>

#include "app_ctx.h"

/* Requires ctx->net_http, ctx->bus, ctx->state, and ctx->settings to
 * already be constructed. */
bool app_wiring_create_denon(app_ctx_t *ctx, time_source_t clock);

#endif /* APP_WIRING_DENON_H */
