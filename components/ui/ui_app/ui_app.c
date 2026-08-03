#include "ui_app.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

#include "ui_app_watchdog.h"

static const char *TAG = "ui_app";

#define UI_DIAG_PUBLISH_PERIOD_US (1 * 1000 * 1000)   /* Tier 7 -- ui_diag_state_t is
                                                          "counters, not rates"; no need
                                                          to publish faster than
                                                          svc_diagnostics's own report
                                                          reads it (DIAGNOSTICS_TICK_
                                                          PERIOD_US, app_wiring.c, 5s) */

#define UI_APP_LOOP_MAX_DELAY_MS 16
#define UI_APP_TASK_STACK_BYTES  (12 * 1024)   /* ARCHITECTURE.md §4: "ui", core 1, prio 4, 12KB */
#define UI_APP_TASK_PRIO         4
#define UI_APP_TASK_CORE         1
/* LVGL's own small intermediate draw buffer for LV_DISPLAY_RENDER_MODE_
 * PARTIAL (hal_display.h's own top comment has the full real-hardware
 * finding behind this switch away from full-frame rendering). 40 rows is
 * not a re-guess -- it's sized to comfortably exceed the real, measured
 * dirty areas this screen actually produces per tick (a moving playhead, a
 * ticking clock, marquee text), while any rarer, larger invalidation (the
 * track-change crossfade, the first full-screen draw) just costs more
 * flush_cb calls to cover, not a correctness problem. PSRAM-backed --
 * this project's own established home for buffers of this kind. */
#define UI_APP_DRAW_BUF_ROWS     40
/* LVGL pulls its own millisecond clock from esp_timer directly, instead of
 * a 2ms periodic esp_timer calling lv_tick_inc() to push it.
 *
 * The old push arrangement cost 500 timer callbacks/second forever -- each
 * one a real esp_timer-task wakeup, context switch, and dispatch, on a
 * device whose whole R1 problem is contention for CPU and the PSRAM bus --
 * purely to increment a counter LVGL only ever reads. lv_tick_set_cb()
 * (managed_components/lvgl__lvgl/src/tick/lv_tick.c, present in this LVGL
 * version) inverts that: lv_tick_get() calls this directly, on demand, from
 * whatever code already wanted the time. Same 1ms resolution LVGL's own
 * lv_tick_get() contract specifies, from a monotonic 64-bit microsecond
 * source, and strictly more accurate than the old version -- lv_tick_inc()
 * advanced in fixed 2ms steps regardless of real elapsed time, so any
 * delayed or coalesced timer callback silently made LVGL's clock run slow
 * (its animations and its LV_LABEL_LONG_MODE_SCROLL marquee both read it).
 *
 * The uint32_t truncation lv_tick_get_cb_t requires is LVGL's own
 * documented contract, not a defect introduced here: it wraps every ~49.7
 * days of uptime, and LVGL compares ticks exclusively through lv_tick_elaps()
 * /lv_tick_diff(), whose unsigned subtraction is wrap-correct by construction
 * (confirmed by reading lv_tick.c directly). lv_tick_inc() accumulated into
 * the same uint32_t and wrapped identically. */
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    ui_app_t *app = (ui_app_t *)lv_display_get_user_data(disp);

    /* px_map is LVGL's own small intermediate draw buffer (LV_DISPLAY_
     * RENDER_MODE_PARTIAL, ui_app_create()), never hal_disp->fb0 itself --
     * esp_lcd_panel_draw_bitmap() (esp_lcd_panel_rgb.c, confirmed by
     * reading it directly) recognizes px_map is NOT one of its own
     * framebuffers and does a real, bounded CPU copy of just this area
     * into fb0 at the right offset. */
    esp_lcd_panel_draw_bitmap(app->hal_disp->panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    app->has_rendered_frame = true;
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    hal_touch_t *touch = (hal_touch_t *)lv_indev_get_user_data(indev);

    data->state = LV_INDEV_STATE_RELEASED;

    if (esp_lcd_touch_read_data(touch->handle) != ESP_OK) {
        return;   /* a transient I2C read failure -- hal_touch's own health
                     * reporting is the place to notice a persistent one,
                     * not this per-poll callback */
    }

    esp_lcd_touch_point_data_t point = { 0 };
    uint8_t                    point_cnt = 0;
    if (esp_lcd_touch_get_data(touch->handle, &point, &point_cnt, 1) == ESP_OK && point_cnt > 0) {
        data->point.x = (int32_t)point.x;
        data->point.y = (int32_t)point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    }
}

static void ui_app_task(void *arg)
{
    ui_app_t *app = (ui_app_t *)arg;

    ui_app_watchdog_register();

    while (!app->task_should_stop) {
        int64_t loop_start_us = esp_timer_get_time();

#ifdef CONFIG_DSC_TEST_FORCE_UI_STALL
        /* RISKS.md R10's rollback proof, same shape as app_wiring.c's own
         * CONFIG_DSC_TEST_FORCE_HEALTH_GATE_TIMEOUT: a deliberate, Kconfig-
         * gated real stall, never fed to the watchdog before it blocks, to
         * prove the task watchdog (CONFIG_ESP_TASK_WDT_TIMEOUT_S=10,
         * sdkconfig.defaults) actually catches a real UI-task freeze and
         * reboots with a coredump -- not just cited as a mitigation.
         * tools/check_release.py refuses a production profile with this
         * set. Every boot re-triggers it on purpose: this build is only
         * ever flashed for the one deliberate test, then reflashed back to
         * a normal build, not left running. */
        ESP_LOGW(TAG, "DSC_TEST_FORCE_UI_STALL set -- UI task will not feed the watchdog for 15s");
        vTaskDelay(pdMS_TO_TICKS(15000));
#endif

        /* drain_event_queue(0) -- no dedicated UI-task event queue exists;
         * view_model_now_playing_apply_tick() drains its own core_eventbus
         * subscriptions directly (never blocking, matching the same "zero
         * timeout" contract this step's name promises). */
        view_model_now_playing_apply_tick(&app->np_apply);
        view_model_denon_apply_tick(&app->denon_apply);
        view_model_connection_apply_tick(&app->connection_apply);
        view_model_system_apply_tick(&app->system_apply);

        uint32_t next_ms = lv_timer_handler();

        int64_t loop_end_us = esp_timer_get_time();
        ui_app_watchdog_assert_loop_duration(loop_start_us, loop_end_us);
        ui_app_watchdog_feed();   /* reached only once lv_timer_handler() has actually
                                     returned -- real observed progress, not a bare
                                     per-iteration feed (ui_app_watchdog.h) */

        /* Tier 7: DOMAIN_UI publish, throttled to ~1/s -- frame/drop
         * counters (hal_display.h) and R17's cumulative glyph-fallback
         * count (view_model_now_playing_apply.h). A legal ui->core edge,
         * no app_wiring mediation needed (svc_diagnostics reads this same
         * domain directly on its own report_tick(), also a legal edge). */
        if (loop_end_us - app->ui_diag_last_publish_us >= UI_DIAG_PUBLISH_PERIOD_US) {
            app->ui_diag_last_publish_us = loop_end_us;
            ui_diag_state_t diag = { 0 };
            hal_display_get_frame_counters(app->hal_disp, &diag.frames_rendered, &diag.frames_dropped);
            diag.unrepresentable_glyph_count = view_model_now_playing_apply_glyph_fallback_count(&app->np_apply);
            core_state_write(app->state, DOMAIN_UI, LINK_STATUS_OK, &diag, sizeof(diag));
        }

        uint32_t delay_ms = (next_ms < UI_APP_LOOP_MAX_DELAY_MS) ? next_ms : UI_APP_LOOP_MAX_DELAY_MS;
        if (delay_ms == 0) {
            delay_ms = 1;   /* vTaskDelay(0) only yields -- force a real tick so a
                                timer that keeps reporting "due now" can't turn
                                this into a busy spin */
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    xSemaphoreGive(app->stopped_sem);
    vTaskDelete(NULL);
}

bool ui_app_create(ui_app_t *app, const ui_app_cfg_t *cfg)
{
    app->hal_disp           = cfg->hal_disp;
    app->hal_touch          = cfg->hal_touch;
    app->state               = cfg->state;
    app->artwork_pixels     = cfg->artwork_pixels;
    app->lv_disp             = NULL;
    app->lv_touch_indev     = NULL;
    app->task_handle        = NULL;
    app->task_should_stop  = false;
    app->stopped_sem         = NULL;
    app->has_rendered_frame = false;
    app->ui_diag_last_publish_us = 0;

    /* Art-forward redesign: no custom heap_caps_malloc()+lv_mem_add_pool()
     * dance here anymore -- see sdkconfig.defaults.esp32s3's own comment on
     * CONFIG_LV_USE_CLIB_MALLOC for the real, confirmed-on-real-hardware
     * reason (lv_mem_add_pool() reliably returned NULL for every PSRAM
     * address tried, at every size tried, while the identical call against
     * internal RAM succeeded). With the C-library allocator, lv_malloc()
     * calls the standard malloc() directly, which CONFIG_SPIRAM_USE_MALLOC=y
     * already transparently backs with PSRAM once internal RAM is
     * exhausted -- lv_init() alone is enough. */
    lv_init();

    /* Before anything that could read the clock (lv_display_create() onward
     * builds real objects and can start timers). Cannot fail and owns no
     * resource, so nothing in the fail path below or in ui_app_teardown()
     * needs to undo it. */
    lv_tick_set_cb(lvgl_tick_get_cb);

    app->lv_disp = lv_display_create(app->hal_disp->h_res, app->hal_disp->v_res);
    if (app->lv_disp == NULL) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return false;
    }
    lv_display_set_user_data(app->lv_disp, app);
    lv_display_set_flush_cb(app->lv_disp, flush_cb);

    size_t draw_buf_bytes = (size_t)app->hal_disp->h_res * UI_APP_DRAW_BUF_ROWS * 2u;   /* RGB565 */
    void  *draw_buf         = heap_caps_malloc(draw_buf_bytes, MALLOC_CAP_SPIRAM);
    if (draw_buf == NULL) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed");
        goto fail;
    }
    lv_display_set_buffers(app->lv_disp, draw_buf, NULL, (uint32_t)draw_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    app->lv_touch_indev = lv_indev_create();
    if (app->lv_touch_indev == NULL) {
        ESP_LOGE(TAG, "lv_indev_create failed");
        goto fail;
    }
    lv_indev_set_type(app->lv_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(app->lv_touch_indev, touch_read_cb);
    lv_indev_set_display(app->lv_touch_indev, app->lv_disp);
    lv_indev_set_user_data(app->lv_touch_indev, app->hal_touch);

    if (!ui_router_create(&app->router, app->lv_disp)) {
        ESP_LOGE(TAG, "ui_router_create failed");
        goto fail;
    }

    /* Now Playing's own screen_now_playing_ctx_t.intent_submit must be the
     * WRAPPED provider (view_model_now_playing_apply's own), not cfg's raw
     * one directly -- that wrapper is the one place that observes every
     * submit() call to start pending/ACK tracking (view_model_now_playing_
     * apply.h's own top comment). Safe to construct before np_apply_init()
     * runs below: it only reads app->np_apply's fields at call time, on a
     * real tap, well after this function returns. */
    app->np_screen.artwork_pixels = cfg->artwork_pixels;
    app->np_screen.intent_submit  = view_model_now_playing_apply_intent_submit_provider(&app->np_apply);
    /* w_volume_slider uses THIS raw provider, not the wrapped one above
     * (w_power_glyph did too, before it and its header control were
     * removed -- screen_now_playing.h's own top comment) -- a real bug
     * found during the Art-forward redesign's own planning: the wrapped
     * provider's wrapped_submit() sets a single screen-wide pending flag
     * on every successful submit regardless of domain, which w_transport_
     * set_state() reads to dim/disable the transport row. Denon volume is
     * a different domain entirely and gives near-instant telnet-echoed
     * feedback, unlike
     * Spotify's poll-confirmed round trip that pending/toast machinery
     * exists for -- routing it through that provider would visibly (and
     * incorrectly) dim the transport row on every volume drag. */
    app->np_screen.intent_submit_raw = cfg->intent_submit;

    /* Every control on the Receiver/Connection screens submits Denon- or
     * settings-domain intents with a near-instant round trip (w_power_row/
     * w_power_glyph, w_volume_slider, w_volume_cap_slider) -- the same
     * reasoning above for np_screen's own raw provider applies here too, so
     * neither screen gets a wrapped variant at all (screen_denon.h's own
     * comment). System is fully read-only, no intent_submit field at all. */
    app->denon_screen.intent_submit_raw      = cfg->intent_submit;
    app->connection_screen.intent_submit_raw = cfg->intent_submit;

    if (!ui_router_register_screen(&app->router, UI_SCREEN_NOW_PLAYING, screen_now_playing_get_interface(), &app->np_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_DENON,       screen_denon_get_interface(), &app->denon_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_CONNECTION,  screen_connection_get_interface(), &app->connection_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_SYSTEM,      screen_system_get_interface(), &app->system_screen)) {
        ESP_LOGE(TAG, "ui_router_register_screen failed");
        goto fail;
    }

    if (!view_model_now_playing_apply_init(&app->np_apply, cfg->state, cfg->bus, &app->router,
                                              &app->np_screen, cfg->intent_submit, cfg->clock)) {
        ESP_LOGE(TAG, "view_model_now_playing_apply_init failed");
        goto fail;
    }

    if (!view_model_denon_apply_init(&app->denon_apply, cfg->state, cfg->bus, &app->router)) {
        ESP_LOGE(TAG, "view_model_denon_apply_init failed");
        goto fail;
    }

    if (!view_model_connection_apply_init(&app->connection_apply, cfg->state, cfg->bus, &app->router)) {
        ESP_LOGE(TAG, "view_model_connection_apply_init failed");
        goto fail;
    }

    if (!view_model_system_apply_init(&app->system_apply, cfg->state, cfg->bus, &app->router, cfg->clock)) {
        ESP_LOGE(TAG, "view_model_system_apply_init failed");
        goto fail;
    }

    app->stopped_sem = xSemaphoreCreateBinary();
    if (app->stopped_sem == NULL) {
        ESP_LOGE(TAG, "xSemaphoreCreateBinary failed");
        goto fail;
    }

    /* Real hardware (Phase 5 Tier 7): by the time ui_app_create() runs
     * (last in app_wiring_create()'s order, after WiFi/svc_denon/
     * svc_spotify/hal_display's own bounce buffers have already claimed
     * internal RAM), the largest contiguous internal block measured only
     * ~11-12 KiB -- under this task's own already-decided 12 KiB stack
     * budget (ARCHITECTURE.md §4). Shrinking the stack to fit would risk a
     * silent overflow instead of an honest allocation failure; PSRAM has
     * ~6.5 MiB free and this project already puts every other large buffer
     * there (framebuffers, artwork JPEG staging/cache), so the stack goes
     * there too via xTaskCreatePinnedToCoreWithCaps() -- the TCB itself
     * still lands in internal RAM automatically (idf_additions.h's own
     * guarantee), only the stack moves. */
    app->task_should_stop = false;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(ui_app_task, "ui_app", UI_APP_TASK_STACK_BYTES, app,
                                                       UI_APP_TASK_PRIO, &app->task_handle, UI_APP_TASK_CORE,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCoreWithCaps failed");
        goto fail;
    }

    return true;

fail:
    if (app->stopped_sem != NULL) {
        vSemaphoreDelete(app->stopped_sem);
        app->stopped_sem = NULL;
    }
    if (app->lv_touch_indev != NULL) {
        lv_indev_delete(app->lv_touch_indev);
        app->lv_touch_indev = NULL;
    }
    if (app->lv_disp != NULL) {
        lv_display_delete(app->lv_disp);   /* also tears down every screen/object
                                               ui_router created against it */
        app->lv_disp = NULL;
    }
    return false;
}

void ui_app_teardown(void *handle)
{
    ui_app_t *app = (ui_app_t *)handle;

    if (app->task_handle != NULL) {
        app->task_should_stop = true;
        /* Bounded by ui_app_task's own loop: at most one lv_timer_handler()
         * pass plus UI_APP_LOOP_MAX_DELAY_MS (16ms) between checks of
         * task_should_stop -- same idiom as svc_denon's teardown. */
        xSemaphoreTake(app->stopped_sem, portMAX_DELAY);
        app->task_handle = NULL;
    }
    if (app->stopped_sem != NULL) {
        vSemaphoreDelete(app->stopped_sem);
        app->stopped_sem = NULL;
    }
    if (app->lv_touch_indev != NULL) {
        lv_indev_delete(app->lv_touch_indev);
        app->lv_touch_indev = NULL;
    }
    if (app->lv_disp != NULL) {
        lv_display_delete(app->lv_disp);
        app->lv_disp = NULL;
    }
}

bool ui_app_has_rendered_frame(const ui_app_t *app)
{
    return app->has_rendered_frame;
}
