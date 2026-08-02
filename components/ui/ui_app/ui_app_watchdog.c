#include "ui_app_watchdog.h"

#include <assert.h>

#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "ui_app_watchdog";

/* Generous relative to the ~16ms steady-state loop cadence
 * (UI_ARCHITECTURE.md §2) -- only meant to catch a genuine stall
 * (R10: "frozen panel"), not to police normal frame-to-frame jitter.
 *
 * Raised from the original 100ms (Tier 7, live real-hardware finding):
 * the first time a real image ever reached w_art_card's render path
 * (every earlier attempt this tier failed before getting this far),
 * LVGL's software renderer scaling the 320x320 decoded artwork up to
 * fill the near-full-screen art card (lv_image_set_inner_align(...,
 * LV_IMAGE_ALIGN_STRETCH), w_art_card.c) measured 285-337ms in a single
 * loop iteration -- this chip has no hardware 2D scaler (confirmed:
 * SOC_PPA_SUPPORTED isn't defined for ESP32-S3, and CONFIG_LV_USE_DRAW_
 * DMA2D isn't enabled), so this is a real, CPU-bound, one-time-per-
 * track-change cost, not a bug to chase away here. Same treatment R1
 * already gives JPEG decode's own worse 660ms-1.8s cost ("the crossfade
 * does not pretend to be instant") -- this is the UI-side sibling of
 * that same real finding, not a new kind of problem. 600ms gives real
 * margin above the observed range without weakening this assertion's
 * real job: catching a GENUINELY unbounded stall (network call, deadlock)
 * still trips this every bit as reliably as before. Properly eliminating
 * the cost -- decoding artwork at final display resolution instead of
 * scaling at render time -- is a real, larger architectural change
 * (cache sizing, memory budget, JPEG decode scale factor all move
 * together), filed as a real follow-up, not done blind here. */
#define UI_APP_LOOP_STALL_THRESHOLD_US (600 * 1000)

void ui_app_watchdog_register(void)
{
    esp_task_wdt_add(NULL);   /* NULL == the calling (UI) task */
}

void ui_app_watchdog_feed(void)
{
    esp_task_wdt_reset();
}

void ui_app_watchdog_assert_loop_duration(int64_t start_us, int64_t end_us)
{
#ifndef NDEBUG
    int64_t duration_us = end_us - start_us;
    /* Logged before the assert can abort the process, so the actual
     * duration survives even through the crash-and-reboot -- found
     * genuinely useful during Tier 7's own live debugging (this line is
     * what confirmed the real 285-337ms art-card render cost, above),
     * kept permanently rather than pulled once that investigation ended. */
    if (duration_us >= UI_APP_LOOP_STALL_THRESHOLD_US) {
        ESP_LOGE(TAG, "loop duration %lld us >= threshold %d us", duration_us,
                  UI_APP_LOOP_STALL_THRESHOLD_US);
    }
    assert(duration_us < UI_APP_LOOP_STALL_THRESHOLD_US);
#else
    (void)start_us;
    (void)end_us;
#endif
}
