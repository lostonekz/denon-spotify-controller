/*
 * Registers the `spotify ...` command family and the top-level `volume
 * <half_db>` command onto the same esp_console REPL debug_console.c already
 * created and is about to start -- see debug_console.c's own call site.
 * Split into its own file from the start (unlike app_wiring.c/
 * debug_console.c, which were split out reactively) since both files are
 * already sized close to the 400-line gate before this phase's own
 * additions. Only compiled in when CONFIG_DSC_SPOTIFY_DEBUG_CONSOLE is set
 * (main/CMakeLists.txt) -- per its own Kconfig help text, this registers
 * onto DSC_DENON_DEBUG_CONSOLE's same REPL rather than standing up a second
 * one, so DSC_DENON_DEBUG_CONSOLE must also be on for there to be a REPL to
 * register onto at all.
 */
#ifndef DEBUG_CONSOLE_SPOTIFY_H
#define DEBUG_CONSOLE_SPOTIFY_H

#include <stdbool.h>

#include "app_ctx.h"

bool debug_console_spotify_register(app_ctx_t *ctx);

#endif /* DEBUG_CONSOLE_SPOTIFY_H */
