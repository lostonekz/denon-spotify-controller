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

/* Periods for the trampolines below. Moved here from app_wiring.c (which
 * was at the 400-line gate) and kept here since: a job's cadence and the
 * function it drives are one fact, and splitting them across two files was
 * how the OTA/artwork relay nearly got registered at the wrong period. */
#define SETTINGS_TICK_PERIOD_US    ((int64_t)1 * 1000000)
#define DIAGNOSTICS_TICK_PERIOD_US ((int64_t)5 * 1000000)
#define NET_TIME_TICK_PERIOD_US    ((int64_t)1 * 1000000)
#define SVC_OTA_TICK_PERIOD_US     ((int64_t)1 * 1000000)
#define HEALTH_GATE_TICK_PERIOD_US ((int64_t)1 * 1000000)
#define DENON_SETTINGS_RELAY_TICK_PERIOD_US ((int64_t)1 * 1000000)
/* No INTENT_DISPATCH_TICK_PERIOD_US anymore: intent dispatch is a task
 * blocking on the queue rather than a periodic job polling it, which beats
 * the ~20-50ms docs/ROADMAP.md Phase 3 asked for outright (dispatch starts
 * on arrival) while removing 50 scheduler wakeups/second. */
#define OTA_ARTWORK_RELAY_TICK_PERIOD_US ((int64_t)1 * 1000000)
/* 250ms, not the 1s every other relay uses: this period is also the
 * touch-to-wake latency, since the wake-touch is swallowed by the UI and
 * only reaches svc_power via this poll. 1s would make a dark panel feel
 * broken. 4 wakeups/second is a twelfth of the 20ms intent-dispatch poll
 * this project already judged wasteful and deleted, for a tick that is two
 * integer comparisons and a usually-empty queue drain. */
#define POWER_TICK_PERIOD_US ((int64_t)250 * 1000)

void settings_tick_trampoline(void *ctx);
void diagnostics_tick_trampoline(void *ctx);
void net_time_tick_trampoline(void *ctx);
void svc_ota_tick_trampoline(void *ctx);
void health_gate_relay_tick_trampoline(void *ctx);
void denon_settings_relay_tick_trampoline(void *ctx);
/* Not a tick trampoline: creates the dedicated task that blocks on
 * ctx->intent_queue and dispatches each intent the moment it arrives.
 * Replaces the 20ms periodic polling job this used to be -- see the
 * function's own comment. Returns false if the task could not be created. */
bool app_wiring_start_intent_dispatch_task(app_ctx_t *ctx);

/* Phase 4: svc_ota already publishes OTA_EVT_PROGRESS/OTA_EVT_FAILED (no
 * new OTA-side publish path needed, verified directly against
 * svc_ota_download.c/svc_ota.c) -- the first PROGRESS seen while not
 * already suspended calls svc_artwork_request_suspend(); FAILED calls
 * svc_artwork_request_resume() -- requests, not direct calls, since
 * suspend/resume are task-owned by "artwork" itself (Phase 5 Tier -1).
 * A successful download never needs an explicit resume -- applying the
 * update reboots the device. */
void ota_artwork_relay_tick_trampoline(void *ctx);

#endif /* APP_WIRING_TICKS_H */
