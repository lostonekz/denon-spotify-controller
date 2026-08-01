/*
 * ui_app -- owns lv_display, the touch input device, the esp_timer LVGL
 * tick, and the UI task itself (ARCHITECTURE.md §4: core 1, prio 4, 12KB
 * stack). "~150 lines. Knows no screen internals." (UI_ARCHITECTURE.md §1).
 *
 * Phase 5 Tier 6: ui_router now carries the real Now Playing screen (Denon/
 * Settings stay stub screens, Phase 6). apply_pending_vm_updates() (UI_
 * ARCHITECTURE.md §2's loop step) is view_model_now_playing_apply_tick() --
 * ui_app itself still never reaches into screen internals; it just owns
 * the glue's storage and drives its tick, the same "owns the task loop,
 * knows nothing about what runs inside each step" shape Tier 4 already had.
 */
#ifndef UI_APP_H
#define UI_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "artwork_pixel_types.h"
#include "core_eventbus.h"
#include "core_state.h"
#include "hal_display.h"
#include "hal_touch.h"
#include "intent_submit_types.h"
#include "lifecycle_types.h"
#include "os_if_types.h"
#include "screen_now_playing.h"
#include "ui_router.h"
#include "view_model_now_playing_apply.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_display_t              *hal_disp;   /* already initialized (Tier 3) */
    hal_touch_t                  *hal_touch;   /* already initialized (Tier 3) */
    artwork_pixel_provider_t         artwork_pixels;   /* Phase 5 plan §1 */
    intent_submit_provider_t           intent_submit;    /* Phase 5 plan §7/adr-0001 --
                                                            * the REAL provider; ui_app_create()
                                                            * wraps it (view_model_now_playing_
                                                            * apply_intent_submit_provider())
                                                            * before handing anything to the screen */
    core_state_t                          *state;    /* already initialized (Phase 1) */
    core_eventbus_t                          *bus;      /* already initialized (Phase 1) */
    time_source_t                               clock;
} ui_app_cfg_t;

typedef struct {
    hal_display_t         *hal_disp;
    hal_touch_t             *hal_touch;
    lv_display_t              *lv_disp;
    lv_indev_t                  *lv_touch_indev;
    artwork_pixel_provider_t       artwork_pixels;

    esp_timer_handle_t     tick_timer;

    ui_router_t               router;

    screen_now_playing_ctx_t       np_screen;   /* Now Playing's own widget instances +
                                                    injected artwork_pixels/intent_submit
                                                    (the WRAPPED provider, not cfg's raw one) */
    view_model_now_playing_apply_t   np_apply;    /* subscriptions + pending/ACK reconciliation --
                                                      driven once per loop iteration, below */

    TaskHandle_t         task_handle;
    volatile bool           task_should_stop;
    SemaphoreHandle_t          stopped_sem;

    volatile bool           has_rendered_frame;   /* set true the first time the flush
                                                      * callback actually runs -- feeds
                                                      * HEALTH_COND_PANEL_UI (app_wiring_ui.c) */
} ui_app_t;

/* cfg->hal_disp/hal_touch/state/bus must already be initialized. Creates
 * the LVGL display bound to the two RGB panel framebuffers directly
 * (LV_DISPLAY_RENDER_MODE_FULL -- no separate LVGL-owned draw buffer, no
 * extra copy), a touch input device backed by hal_touch's GT911 handle,
 * the 2ms esp_timer LVGL tick, ui_router (Now Playing real, Denon/Settings
 * still stub), and the dedicated UI task (core 1, prio 4, 12KB stack,
 * ARCHITECTURE.md §4). */
bool ui_app_create(ui_app_t *app, const ui_app_cfg_t *cfg);
void ui_app_teardown(void *handle);

/* True once the flush callback has run at least once -- a real, observed
 * "a frame actually reached the panel" signal, not just "init returned
 * ESP_OK." Feeds HEALTH_COND_PANEL_UI. */
bool ui_app_has_rendered_frame(const ui_app_t *app);

/* Same reasoning as hal_display.h's SVC_HAL_DISPLAY_SERVICE_DESC: reboot,
 * degraded_ok=false (LIFECYCLE.md §1.1, Phase 5 plan §9), no restart path
 * built for this component. requires both hal_display and hal_touch --
 * ui_app_create() dereferences cfg->hal_disp/hal_touch unconditionally, so
 * the boot graph must guarantee they're already up. */
static const service_desc_t SVC_UI_SERVICE_DESC = {
    .id           = SVC_ID_UI,
    .name         = "ui",
    .requires     = SVC_BIT(SVC_ID_HAL_DISPLAY) | SVC_BIT(SVC_ID_HAL_TOUCH),
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = false,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* UI_APP_H */
