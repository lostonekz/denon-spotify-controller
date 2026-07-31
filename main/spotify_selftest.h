/*
 * Scripted svc_spotify pairing, run once after boot. See
 * Kconfig.projbuild's DSC_SPOTIFY_AUTOPAIR for why this exists (same
 * reasoning as denon_selftest.h's DSC_DENON_SELFTEST). Only compiled in
 * when that flag is set (main/CMakeLists.txt).
 */
#ifndef SPOTIFY_SELFTEST_H
#define SPOTIFY_SELFTEST_H

#include "app_ctx.h"

/* Spawns a one-shot task that waits for net_time sync, begins pairing
 * (svc_spotify_begin_pair()) if not already paired, logs the authorize URL
 * via ESP_LOGI, and -- if CONFIG_DSC_SPOTIFY_AUTOPAIR_CODE is non-empty --
 * submits it (svc_spotify_submit_pasted_url()) to complete the exchange.
 * Logs session status until ACTIVE/REAUTH_REQUIRED or a timeout, so a
 * read-only UART capture is enough to verify the whole sequence. */
void spotify_selftest_start(app_ctx_t *ctx);

#endif /* SPOTIFY_SELFTEST_H */
