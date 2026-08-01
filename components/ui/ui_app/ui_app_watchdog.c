#include "ui_app_watchdog.h"

#include <assert.h>

#include "esp_task_wdt.h"

/* Generous relative to the ~16ms steady-state loop cadence
 * (UI_ARCHITECTURE.md §2) -- only meant to catch a genuine stall
 * (R10: "frozen panel"), not to police normal frame-to-frame jitter. */
#define UI_APP_LOOP_STALL_THRESHOLD_US (100 * 1000)

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
    assert(duration_us < UI_APP_LOOP_STALL_THRESHOLD_US);
#else
    (void)start_us;
    (void)end_us;
#endif
}
