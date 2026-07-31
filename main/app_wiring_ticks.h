/*
 * svc_scheduler tick trampolines app_wiring.c registers via
 * svc_scheduler_post_periodic(). Split out of app_wiring.c purely for the
 * 400-line gate (check_limits.py) -- these are still part of the
 * composition root, not a reusable component; not installed, no other
 * file includes this.
 */
#ifndef APP_WIRING_TICKS_H
#define APP_WIRING_TICKS_H

#include "app_ctx.h"

void settings_tick_trampoline(void *ctx);
void diagnostics_tick_trampoline(void *ctx);
void net_time_tick_trampoline(void *ctx);
void svc_ota_tick_trampoline(void *ctx);
void health_gate_relay_tick_trampoline(void *ctx);
void denon_settings_relay_tick_trampoline(void *ctx);
void intent_dispatch_tick_trampoline(void *ctx);

/* Phase 4: svc_ota already publishes OTA_EVT_PROGRESS/OTA_EVT_FAILED (no
 * new OTA-side publish path needed, verified directly against
 * svc_ota_download.c/svc_ota.c) -- the first PROGRESS seen while not
 * already suspended calls svc_artwork_suspend(); FAILED calls
 * svc_artwork_resume(). A successful download never needs an explicit
 * resume -- applying the update reboots the device. */
void ota_artwork_relay_tick_trampoline(void *ctx);

#endif /* APP_WIRING_TICKS_H */
