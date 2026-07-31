/*
 * The UART debug console -- docs/ROADMAP.md Phase 3's "crude debug UI,"
 * implemented as an esp_console REPL rather than on-screen text (no
 * hal_display/LVGL yet; see the plan for the full reasoning). Only
 * compiled in when CONFIG_DSC_DENON_DEBUG_CONSOLE is set
 * (main/CMakeLists.txt); tools/check_release.py refuses a production
 * profile with the flag on.
 */
#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <stdbool.h>

#include "app_ctx.h"

bool debug_console_start(app_ctx_t *ctx);

#endif /* DEBUG_CONSOLE_H */
