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

#endif /* APP_WIRING_TICKS_H */
