/*
 * The UI task's own watchdog + stall assertion -- RISKS.md R10: "one
 * blocking call in an LVGL event handler and the panel is frozen... on a
 * wall device with no console, that is indistinguishable from dead
 * hardware." Mitigation: "task watchdog on every task... a debug assertion
 * on loop duration." Both implemented for real here, not just cited.
 */
#ifndef UI_APP_WATCHDOG_H
#define UI_APP_WATCHDOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Subscribes the calling task (the UI task) to the already-initialized
 * task watchdog (CONFIG_ESP_TASK_WDT_INIT, sdkconfig.defaults). Call once,
 * from the UI task itself, before its loop starts. */
void ui_app_watchdog_register(void);

/* Feeds the watchdog -- call ONLY when the loop iteration observed real
 * progress (an event drained, a frame rendered), never unconditionally
 * once per bare iteration. A UI task that's spinning through empty
 * iterations without lv_timer_handler() ever actually doing anything is
 * exactly the stall this watchdog exists to catch; feeding it
 * unconditionally would defeat that. */
void ui_app_watchdog_feed(void);

/* Debug-build-only stall assertion (UI_ARCHITECTURE.md §2's hard rule,
 * enforced "by review and by a debug-build assertion on loop duration").
 * start_us/end_us should bracket one full loop iteration (drain events +
 * apply vm updates + lv_timer_handler + the loop's own bookkeeping, NOT
 * including the vTaskDelay at the end). Threshold is generous relative to
 * the ~16ms steady-state cadence -- this is a stall detector, not a
 * frame-budget profiler; RISKS.md's own artwork-crossfade numbers (up to
 * 1.8s JPEG decode) are svc_artwork's problem, never this task's, so this
 * threshold only needs to catch a genuine multi-frame freeze. No-op
 * entirely in a release build (assert() itself compiles out under
 * NDEBUG). */
void ui_app_watchdog_assert_loop_duration(int64_t start_us, int64_t end_us);

#ifdef __cplusplus
}
#endif

#endif /* UI_APP_WATCHDOG_H */
