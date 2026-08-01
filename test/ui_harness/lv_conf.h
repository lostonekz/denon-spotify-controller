/*
 * Host-only LVGL config for test/ui_harness (Phase 5 Tier 5) -- deliberately
 * minimal, not a full copy of LVGL's own lv_conf_template.h: every setting
 * this file doesn't define falls back to lv_conf_internal.h's own default
 * (the same one the un-modified template ships), so only the handful of
 * settings that actually need to differ from that default are listed here.
 * The real target build never uses this file at all -- ESP-IDF's LVGL port
 * is Kconfig-driven (sdkconfig.defaults' CONFIG_LV_COLOR_DEPTH_16 etc.,
 * consumed via managed_components/lvgl__lvgl/env_support/cmake/esp.cmake),
 * this is the plain-CMake-desktop-build equivalent, used only here.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* LV_USE_SDL is 0 by default -- the target build never enables it (no SDL2
 * on the ESP32-S3), this desktop harness is the only place it's needed. */
#define LV_USE_SDL 1

/* FetchContent-vendored SDL2 (test/ui_harness/CMakeLists.txt) exposes its
 * raw source-tree include/ dir directly -- SDL.h lives right under it, not
 * nested under an SDL2/ subdirectory the way an installed system package
 * (and lv_conf_internal.h's own default, <SDL2/SDL.h>) would assume. */
#define LV_SDL_INCLUDE_PATH <SDL.h>

#endif /* LV_CONF_H */
