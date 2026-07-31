/*
 * Scripted svc_denon self-test, run once after boot. See
 * Kconfig.projbuild's DSC_DENON_SELFTEST for why this exists. Only
 * compiled in when that flag is set (main/CMakeLists.txt).
 */
#ifndef DENON_SELFTEST_H
#define DENON_SELFTEST_H

#include "app_ctx.h"

/* Spawns a one-shot task that waits for the link to come up, then submits
 * a scripted sequence of real intents through app_wiring_submit_intent(),
 * ending with a fuzzed burst of volume intents. Logs each step via
 * ESP_LOGI so a UART capture is enough to verify the whole sequence. */
void denon_selftest_start(app_ctx_t *ctx);

#endif /* DENON_SELFTEST_H */
