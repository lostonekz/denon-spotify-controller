/*
 * Phase 6: svc_power's composition-root half.
 *
 * svc_power decides WHEN the panel sleeps; this file is the only thing that
 * actually turns the backlight off, because `svc` may not depend on `hal`
 * (tools/arch_rules.json) and app is the only layer that can see both. The
 * fourth instance of the mediator-trampoline pattern in this composition
 * root, after the health gate, the Denon settings relay and the
 * OTA/artwork relay.
 *
 * Split into its own file rather than added to app_wiring_ticks.c for the
 * usual reason (the 400-line gate), and because create-plus-subscribe-plus-
 * schedule for one concern reads better in one place than spread across
 * app_wiring.c and app_wiring_ticks.c.
 */
#ifndef APP_WIRING_POWER_H
#define APP_WIRING_POWER_H

#include "app_ctx.h"

/* Creates svc_power, subscribes app_wiring to TOPIC_POWER, and registers
 * the periodic tick. Must be called AFTER app_wiring_create_ui(): the tick
 * reads ui_app_touch_idle_ms() and the scheduler worker is already running
 * by this point, so a job registered here can fire immediately.
 *
 * False on any failure, all boot-fatal at the call site: svc_power's own
 * TOPIC_PLAYBACK subscribe, this file's TOPIC_POWER subscribe, or the
 * scheduler job. */
bool app_wiring_create_power(app_ctx_t *ctx, time_source_t clock);

#endif /* APP_WIRING_POWER_H */
