/*
 * bsp_board (I2C bus + shared CH422G IO expander) -> hal_display -> hal_touch
 * -> hal_backlight (enabled immediately, static/fixed-on -- Phase 6's
 * svc_power owns dimming) -> ui_app (LVGL display/indev/tick/task, which
 * also creates and drives ui_router internally -- see ui_app.h) in that
 * order. Split out of app_wiring.c purely for the 400-line gate, same as
 * app_wiring_denon.h/app_wiring_spotify.h.
 */
#ifndef APP_WIRING_UI_H
#define APP_WIRING_UI_H

#include <stdbool.h>

#include "app_ctx.h"

/* Requires ctx->svc_artwork to already be constructed (its pixel provider
 * is threaded into ui_app's config) and ctx->bus/ctx->state to already
 * exist (Phase 1 -- always true by the time this runs). */
bool app_wiring_create_ui(app_ctx_t *ctx, time_source_t clock);

/* hal_display/hal_touch (layer "hal") cannot include lifecycle_types.h
 * themselves -- tools/arch_rules.json's hal -> [bsp, idf] edge list has no
 * core entry, unlike svc's own hal -> core exception each svc_*_service_
 * desc() relies on. app (allowed to depend on everything) builds these
 * descriptors instead; ui_app.h's own SVC_UI_SERVICE_DESC has no such
 * restriction (ui -> core IS an allowed edge) and stays defined there.
 *
 * LIFECYCLE.md §1.1's restart table already anticipates both rows (Phase 5
 * plan §9): reboot, degraded_ok=false -- matches SVC_STORAGE_SERVICE_DESC's
 * exact shape, the other such subsystem. No teardown/restart_fn: neither
 * component was ever given a restart path (Tier 3's scope), matching
 * lifecycle_types.h's own "teardown not provably complete -- reboot"
 * reasoning for RESTART_REBOOT. */
static const service_desc_t SVC_HAL_DISPLAY_SERVICE_DESC = {
    .id           = SVC_ID_HAL_DISPLAY,
    .name         = "hal_display",
    .requires     = 0,
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = false,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

static const service_desc_t SVC_HAL_TOUCH_SERVICE_DESC = {
    .id           = SVC_ID_HAL_TOUCH,
    .name         = "hal_touch",
    .requires     = 0,
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = false,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#endif /* APP_WIRING_UI_H */
