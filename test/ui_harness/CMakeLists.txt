# SDL2 + LVGL "simulator" harness for components/ui/. Phase 5 Tier 5.
#
# Manually run, not part of test/host's ctest suite -- docs/ROADMAP.md Phase
# 5 plan: this exists so screen/widget iteration happens against a desktop
# window first, before real hardware is needed, same "not a routine build
# step" spirit as tools/gen_assets.py.
#
#   cmake -S test/ui_harness -B build_ui_harness
#   cmake --build build_ui_harness
#   ./build_ui_harness/ui_harness [fixture-name]
#
# Deliberately a plain CMake project, not an ESP-IDF one -- links the SAME
# ui_router/screens/widgets source files ESP-IDF also compiles into
# component "ui", against a real host-native SDL2 window instead of the RGB
# panel, and against fake core_state/core_eventbus fixtures instead of real
# hal/services (Phase 5 plan's directory-structure comment).

cmake_minimum_required(VERSION 3.16)
project(dsc_ui_harness C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

include(FetchContent)

# --- SDL2 -------------------------------------------------------------
# Vendored via FetchContent and built from source with the same compiler as
# everything else here -- this dev machine has no SDL2 dev package
# installed, and building from source keeps a fresh clone working with no
# manual install step. Pinned to an exact tag (docs/RISKS.md R9, same
# reasoning as LVGL/Unity): an unpinned dependency is one that changes
# underneath you. Static + no test lib: this is a single manually-run
# executable, not something that needs a redistributable SDL2 DLL sitting
# next to it.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL2)

# --- LVGL ---------------------------------------------------------------
# The SAME vendored LVGL tree ESP-IDF builds (managed_components/lvgl__lvgl),
# built here for host via LVGL's own official desktop CMake support
# (env_support/cmake/os_desktop.cmake), against this directory's own
# lv_conf.h (LV_USE_SDL 1 -- the target build never enables this driver).
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" lvgl_build)

# lv_sdl_*.c (compiled unconditionally into the lvgl target, gated internally
# on #if LV_USE_SDL) need SDL2's headers while THEY compile, not just
# ui_harness -- PUBLIC so it also reaches anything that links "lvgl".
target_link_libraries(lvgl PUBLIC SDL2-static)

# --- ui_harness -----------------------------------------------------------
# Real production sources -- the exact files ESP-IDF also compiles into
# component "ui" (Tier 6: view_model/widgets/screen_now_playing.c added,
# same files components/ui/CMakeLists.txt lists), plus this harness's own
# host-only bring-up/fixtures.
add_executable(ui_harness
    main.c
    fake_artwork_pixel_provider.c
    fake_intent_submit_provider.c
    fake_system_apply.c
    fixtures/fixtures.c
    fixtures/fixture_artwork_pixels.c
    "${REPO_ROOT}/components/ui/view_model/view_model_tone.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_format.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_text.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_progress.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_now_playing.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_now_playing_apply.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_denon.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_denon_apply.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_connection.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_connection_apply.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_system.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_pair.c"
    "${REPO_ROOT}/components/ui/view_model/view_model_pair_apply.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_display.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_hero.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_title.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_body.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_label.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_meta.c"
    "${REPO_ROOT}/components/ui/assets/fonts/ui_font_meta_lg.c"
    "${REPO_ROOT}/components/ui/ui_router/ui_router.c"
    "${REPO_ROOT}/components/ui/widgets/w_page_edge_tab.c"
    "${REPO_ROOT}/components/ui/widgets/w_power_glyph.c"
    "${REPO_ROOT}/components/ui/widgets/w_power_row.c"
    "${REPO_ROOT}/components/ui/widgets/w_source_status_card.c"
    "${REPO_ROOT}/components/ui/widgets/w_info_row.c"
    "${REPO_ROOT}/components/ui/widgets/w_volume_cap_slider.c"
    "${REPO_ROOT}/components/ui/widgets/w_status_pill.c"
    "${REPO_ROOT}/components/ui/widgets/w_stroke_label.c"
    "${REPO_ROOT}/components/ui/widgets/w_marquee_label.c"
    "${REPO_ROOT}/components/ui/widgets/w_progress_bar.c"
    "${REPO_ROOT}/components/ui/widgets/w_transport.c"
    "${REPO_ROOT}/components/ui/widgets/w_volume_slider.c"
    "${REPO_ROOT}/components/ui/widgets/w_toast.c"
    "${REPO_ROOT}/components/ui/widgets/w_art_card.c"
    "${REPO_ROOT}/components/ui/widgets/w_qr.c"
    "${REPO_ROOT}/components/ui/screens/screen_now_playing.c"
    "${REPO_ROOT}/components/ui/screens/screen_denon.c"
    "${REPO_ROOT}/components/ui/screens/screen_connection.c"
    "${REPO_ROOT}/components/ui/screens/screen_system.c"
    "${REPO_ROOT}/components/ui/screens/screen_pair.c"
    "${REPO_ROOT}/components/core_state/core_state.c"
    "${REPO_ROOT}/components/core_eventbus/core_eventbus.c"
    "${REPO_ROOT}/components/core_pool/core_pool_ring.c"
    "${REPO_ROOT}/test/host/fake_lock.c"
    "${REPO_ROOT}/test/host/fake_time_source.c")

target_compile_definitions(ui_harness PRIVATE "LV_LVGL_H_INCLUDE_SIMPLE")

target_include_directories(ui_harness PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/fixtures"
    "${REPO_ROOT}/components/ui/include"
    "${REPO_ROOT}/components/core_types/include"
    "${REPO_ROOT}/components/core_state/include"
    "${REPO_ROOT}/components/core_eventbus/include"
    "${REPO_ROOT}/components/core_pool/include"
    "${REPO_ROOT}/test/host")

target_link_libraries(ui_harness PRIVATE lvgl)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ui_harness PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    # SDL2's Windows backend needs these even when statically linked --
    # SDL2-static's own CMake target does not always propagate them via
    # INTERFACE_LINK_LIBRARIES depending on the exact SDL2 version/generator.
    target_link_libraries(ui_harness PRIVATE setupapi winmm imm32 version)

    # SDL2-static's own CMake requests the GUI subsystem (its conventional
    # SDL2main/WinMain app shape), which propagates transitively through
    # "lvgl" into this link and leaves the mingw CRT looking for a WinMain
    # symbol that doesn't exist -- a real link failure hit while building
    # this ("undefined symbol: WinMain", referenced by crtexewin.c). Forcing
    # the console subsystem here overrides that, matching main.c's own
    # SDL_MAIN_HANDLED (a plain console `main`, stdout/stderr going straight
    # to the terminal -- this is a manually-run dev tool, not a GUI app).
    target_link_options(ui_harness PRIVATE -mconsole)
endif()
