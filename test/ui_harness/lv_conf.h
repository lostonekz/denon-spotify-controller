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

/* --- settings that must MATCH the target build, not LVGL's defaults ---
 *
 * The "only list what differs from the default" principle in this file's
 * top comment is still right, but it was being applied against the wrong
 * baseline: what matters is differing from THE TARGET BUILD, since a
 * harness that renders differently from the device is worth very little.
 * Each of the three below was verified against the real generated
 * sdkconfig (CONFIG_LV_* values), not assumed.
 */

/* Target: CONFIG_LV_USE_FONT_COMPRESSED=y. LVGL's own default is 0.
 *
 * This one is not a nuance, it is the difference between a working harness
 * and a blank screen: every font tools/gen_assets.py generates is emitted
 * in lv_font_conv's compressed bitmap format (`.bitmap_format = 1` in the
 * generated .c files, confirmed directly), and with decompression disabled
 * lv_font_get_bitmap_fmt_txt() returns NULL for every glyph -- so every
 * label using one of this project's own fonts renders completely empty,
 * while LVGL's built-in montserrat (used for nothing on these screens)
 * would render fine. That is the exact real-hardware bug documented in
 * sdkconfig.defaults' own CONFIG_LV_USE_FONT_COMPRESSED comment, and this
 * harness was silently configured to reproduce it. */
#define LV_USE_FONT_COMPRESSED 1

/* Target: CONFIG_LV_USE_THEME_DEFAULT=y. LVGL's default in this version is 0.
 *
 * ui_router.c and screen_now_playing.c both carry substantial comments
 * about real bugs caused by the default theme's own styling (an uncleared
 * radius leaving screen corners un-covered, a nonzero pad_row/pad_column on
 * the pager throwing off page scroll maths). With the theme off here, none
 * of that class of bug can reproduce in the harness -- it would render
 * correctly on the desktop and wrong on the device, which is precisely the
 * failure mode a harness exists to prevent. */
#define LV_USE_THEME_DEFAULT 1

/* Target: CONFIG_LV_USE_CLIB_MALLOC=y, i.e. plain malloc/realloc/free with
 * no fixed pool. LVGL's default here is LV_STDLIB_BUILTIN, whose companion
 * LV_MEM_SIZE default is 64 KiB.
 *
 * 64 KiB is far too small for these screens: docs/MEMORY.md §3 records that
 * the Art-forward Now Playing screen needed the pool raised past 512 KiB on
 * target (LVGL's software renderer composites layers in the layer's own
 * color format, and a near-full-screen layer is ~900 KiB at ARGB8888).
 * Left at the builtin pool, harness widget construction would fail on
 * allocation and main.c would exit with "ui_router_register_screen failed"
 * -- or, with LV_USE_ASSERT_MALLOC on, hang in LVGL's own bare while(1)
 * OOM handler. Matching the target's allocator is both the more faithful
 * and the working choice. LV_USE_ASSERT_MALLOC is deliberately left at its
 * host default of 0: every widget in this codebase already null-checks and
 * propagates allocation failure, so a clean error exit beats a hang when
 * debugging on a desktop. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

/* Target: CONFIG_LV_USE_QRCODE=y (sdkconfig.defaults). LVGL's default is 0.
 *
 * The pair overlay's QR code. This one has to be listed here explicitly for
 * the reason this whole file exists: the harness does NOT read sdkconfig at
 * all, so a Kconfig flip on the target side is invisible here -- and w_qr.c
 * would simply fail to compile (lv_qrcode.h #if's its entire API away),
 * taking the one place a QR can be checked against a real phone camera
 * before flashing with it. */
#define LV_USE_QRCODE 1

#endif /* LV_CONF_H */
