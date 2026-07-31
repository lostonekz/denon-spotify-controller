/*
 * svc_spotify construction (Kconfig-seeds client_id/redirect_uri into
 * secure_kv only if nothing is stored yet, loads any existing
 * credentials/AVR pin, builds svc_spotify_cfg_t, calls svc_spotify_create(),
 * retains the restart ctx). Split out of app_wiring.c purely for the
 * 400-line gate, same as app_wiring_denon.h.
 */
#ifndef APP_WIRING_SPOTIFY_H
#define APP_WIRING_SPOTIFY_H

#include <stdbool.h>

#include "app_ctx.h"

/* Requires ctx->net_http, ctx->net_time, ctx->bus, ctx->state, and
 * ctx->secure_kv to already be constructed. No svc_settings dependency --
 * PLATFORM.md §3.4: Spotify has no user-facing setting, auth lives
 * entirely in the secure namespace. */
bool app_wiring_create_spotify(app_ctx_t *ctx, time_source_t clock);

#endif /* APP_WIRING_SPOTIFY_H */
