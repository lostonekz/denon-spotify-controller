# main — composition root (app layer).
#
# The app layer is the ONLY layer allowed to depend on everything, because it
# is the only place composite ownership lives (docs/ARCHITECTURE.md §1.3).
# REQUIRES grows as app_wiring.c picks up each later phase's components.

set(DSC_MAIN_SRCS "main.c" "app_start.c" "app_wiring.c" "app_wiring_core.c" "app_wiring_ticks.c"
                   "app_wiring_denon.c" "app_wiring_spotify.c" "app_wiring_artwork.c"
                   "app_wiring_ui.c"
                   "app_wiring_volume.c" "volume_coordinator.c")

# debug_console.c (docs/ROADMAP.md Phase 3's UART debug UI) only enters
# SRCS -- and its esp_console_*() calls only ever get linked in -- when the
# dev explicitly opts in (CONFIG_DSC_DENON_DEBUG_CONSOLE). "console" itself
# is listed in REQUIRES unconditionally below: ESP-IDF evaluates component
# REQUIRES in an early pass that does not reliably see CONFIG_ values (a
# conditional list(APPEND ... REQUIRES "console") inside if(CONFIG_...)
# silently failed to add the include path -- confirmed via
# build/compile_commands.json showing no -I for components/console at all
# despite the if() being taken), so gating REQUIRES itself doesn't work.
# This costs nothing when the flag is off: nothing in "main" references
# any console symbol unless debug_console.c is actually compiled in, and
# an unreferenced static-archive component is not pulled into the final
# link.
if(CONFIG_DSC_DENON_DEBUG_CONSOLE)
    list(APPEND DSC_MAIN_SRCS "debug_console.c")
endif()

# debug_console_spotify.c: same story -- only enters SRCS when
# CONFIG_DSC_SPOTIFY_DEBUG_CONSOLE is set. It registers onto the REPL
# debug_console.c creates (see debug_console_spotify.h), so it is only ever
# useful (and only ever referenced) alongside CONFIG_DSC_DENON_DEBUG_CONSOLE,
# but is gated on its own flag independently, same as the Kconfig text says.
if(CONFIG_DSC_SPOTIFY_DEBUG_CONSOLE)
    list(APPEND DSC_MAIN_SRCS "debug_console_spotify.c" "debug_console_spotify_playback.c")
endif()

# denon_selftest.c: same story as debug_console.c above -- only enters SRCS
# when CONFIG_DSC_DENON_SELFTEST is set. No new REQUIRES needed (uses only
# FreeRTOS + svc_denon + app_wiring, already available).
if(CONFIG_DSC_DENON_SELFTEST)
    list(APPEND DSC_MAIN_SRCS "denon_selftest.c")
endif()

# spotify_selftest.c: same story, gated on CONFIG_DSC_SPOTIFY_AUTOPAIR --
# scripting interactive console input over this board's native USB-CDC port
# proved unreliable from Windows for Spotify pairing too, same reasoning as
# DSC_DENON_SELFTEST. No new REQUIRES needed (uses only FreeRTOS +
# svc_spotify + app_ctx, already available).
if(CONFIG_DSC_SPOTIFY_AUTOPAIR)
    list(APPEND DSC_MAIN_SRCS "spotify_selftest.c")
endif()

idf_component_register(
    SRCS        ${DSC_MAIN_SRCS}
    INCLUDE_DIRS "."
    REQUIRES     "core_types" "core_eventbus" "core_state"
                 "svc_storage" "svc_scheduler" "svc_logging" "svc_settings"
                 "svc_diagnostics" "svc_supervisor"
                 "net_wifi" "net_time" "net_http" "svc_ota" "svc_denon"
                 "svc_spotify" "svc_artwork"
                 "bsp_board" "hal_display" "hal_touch" "hal_backlight" "ui"
                 "console"
    PRIV_REQUIRES "esp_netif" "esp_event"
)
