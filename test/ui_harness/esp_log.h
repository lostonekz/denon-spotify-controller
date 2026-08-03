/*
 * Host-only ESP_LOG* shim for test/ui_harness.
 *
 * Several files in component `ui` that this harness compiles verbatim
 * (w_volume_slider.c, w_power_glyph.c, screen_now_playing.c,
 * screen_denon.c) #include "esp_log.h" for real diagnostics -- a widget
 * that logs why it failed to build is worth more than one that fails
 * silently, so the right fix is to satisfy the include on host rather than
 * to strip logging out of shared production sources for the harness's
 * benefit.
 *
 * Deliberately macros only, no .c file: ESP_LOGE/W/D are the only three
 * variants any harness-compiled ui file actually uses (verified by grep
 * across widgets/, screens/, view_model/, ui_router/), and none of them
 * touches any other esp_* symbol at all -- so there is nothing here to
 * link, and this stays a single header with no build-system entry.
 *
 * Levels map to stderr rather than stdout so ordinary harness output
 * (main.c's own fixture banner, printed to stdout) stays separable from
 * diagnostics -- `ui_harness 2>/dev/null` gives a clean run, and a real
 * widget-construction failure is still visible in a terminal by default.
 *
 * ESP_LOGD is compiled out by default, matching the target's own
 * CONFIG_LOG_DEFAULT_LEVEL_INFO: the DEBUG-level lines in w_volume_slider.c
 * fire on every LV_EVENT_VALUE_CHANGED during a drag, and having those
 * scroll past continuously by default would make the harness less usable,
 * not more. Define UI_HARNESS_LOG_DEBUG at compile time to get them:
 *
 *   cmake -S test/ui_harness -B build_ui_harness -DCMAKE_C_FLAGS=-DUI_HARNESS_LOG_DEBUG
 */
#ifndef UI_HARNESS_ESP_LOG_H
#define UI_HARNESS_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)

#ifdef UI_HARNESS_LOG_DEBUG
#define ESP_LOGD(tag, fmt, ...) fprintf(stderr, "D (%s) " fmt "\n", tag, ##__VA_ARGS__)
#else
/* Consumes its arguments in a never-executed branch so they still get type
 * checked (an unused-variable or format-mismatch bug in a debug-only log
 * line should not be invisible on host), while the optimizer drops the
 * whole thing. Same technique ESP-IDF's own esp_log.h uses for levels
 * above CONFIG_LOG_MAXIMUM_LEVEL. */
#define ESP_LOGD(tag, fmt, ...)                                              \
    do {                                                                     \
        if (0) {                                                             \
            fprintf(stderr, "D (%s) " fmt "\n", tag, ##__VA_ARGS__);         \
        }                                                                    \
    } while (0)
#endif

#define ESP_LOGV(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)

#endif /* UI_HARNESS_ESP_LOG_H */
