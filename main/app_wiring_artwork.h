/*
 * svc_artwork construction. Split out of app_wiring.c purely for the
 * 400-line gate, same as app_wiring_denon.h/app_wiring_spotify.h.
 */
#ifndef APP_WIRING_ARTWORK_H
#define APP_WIRING_ARTWORK_H

#include <stdbool.h>

#include "app_ctx.h"

/* Requires ctx->net_http, ctx->bus, and ctx->state to already be
 * constructed. No svc_settings/svc_storage dependency -- artwork has no
 * user-facing setting and no persisted secret. */
bool app_wiring_create_artwork(app_ctx_t *ctx, time_source_t clock);

#endif /* APP_WIRING_ARTWORK_H */
