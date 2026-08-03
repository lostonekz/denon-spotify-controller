/*
 * SDL2 + LVGL simulator harness -- Phase 5 Tiers 5/6. Runs the SAME
 * ui_router.c/screen_now_playing.c/widgets ESP-IDF also compiles into
 * component "ui", against a desktop SDL2 window instead of the RGB panel,
 * a fake core_state snapshot instead of real services, and a fake intent-
 * submit provider that simulates an immediate-accept backend. See
 * test/ui_harness/CMakeLists.txt's top comment for how to build/run it.
 */
#include <stdio.h>

/* Without this, SDL.h #defines main to SDL_main and expects SDL2main's own
 * WinMain to provide the real entry point (a real link failure hit while
 * building this on Windows: "undefined symbol: WinMain"). This harness
 * links SDL2 directly, not SDL2main, and wants a plain console entry point
 * with stdout/stderr going straight to the terminal -- SDL_MAIN_HANDLED
 * tells SDL.h to leave `main` alone. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <lvgl.h>

#include "core_eventbus.h"
#include "core_state.h"
#include "fake_artwork_pixel_provider.h"
#include "fake_intent_submit_provider.h"
#include "fake_lock.h"
#include "fake_time_source.h"
#include "fixtures.h"
#include "screen_now_playing.h"
#include "screen_stub.h"
#include "ui_router.h"
#include "view_model_now_playing_apply.h"

#define UI_HARNESS_H_RES 480
#define UI_HARNESS_V_RES 480
#define UI_HARNESS_LOOP_MAX_DELAY_MS 16

/* Depths/policies mirror main/app_wiring_core.c's real values for these
 * same topics exactly, not arbitrary numbers. */
#define UI_HARNESS_BUS_MAX_SUBS 8

static void print_usage(void)
{
    fprintf(stderr, "usage: ui_harness [fixture-name]\n\navailable fixtures:\n");
    for (int i = 0; i < UI_HARNESS_FIXTURE_COUNT; i++) {
        fprintf(stderr, "  %s\n", ui_harness_fixture_name((ui_harness_fixture_t)i));
    }
}

int main(int argc, char **argv)
{
    ui_harness_fixture_t fixture = UI_HARNESS_FIXTURE_NORMAL;
    if (argc > 1) {
        if (!ui_harness_fixture_from_name(argv[1], &fixture)) {
            fprintf(stderr, "ui_harness: unknown fixture '%s'\n\n", argv[1]);
            print_usage();
            return 1;
        }
    }

    /* --- fake core_state, seeded from the chosen fixture --- */
    fake_time_source_t fts = { .now_us = 0 };
    core_state_t        state;
    core_state_init(&state, fake_lock_get(), fake_time_source_get(&fts));

    const uint16_t *pixels = NULL;
    ui_harness_fixture_apply(&state, fixture, &pixels);

    fake_artwork_pixel_provider_t fake_artwork = { 0 };
    fake_artwork_pixel_provider_set_pattern(&fake_artwork, pixels);
    artwork_pixel_provider_t artwork_provider = fake_artwork_pixel_provider_get(&fake_artwork);

    eventbus_sub_t      bus_subs[UI_HARNESS_BUS_MAX_SUBS];
    const topic_policy_t bus_policies[] = {
        { .topic = TOPIC_DENON,      .depth = 8, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
        { .topic = TOPIC_PLAYBACK,   .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
        { .topic = TOPIC_ARTWORK,    .depth = 2, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
        { .topic = TOPIC_INTENT_ACK, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
        { .topic = TOPIC_TOAST,      .depth = 4, .on_full = EVENT_OVERFLOW_DROP_NEWEST,    .on_release = NULL, .release_ctx = NULL },
    };
    core_eventbus_t bus;
    core_eventbus_init(&bus, bus_subs, UI_HARNESS_BUS_MAX_SUBS, bus_policies, 5, fake_lock_get());

    fake_intent_submit_provider_t fake_intent = { 0 };
    fake_intent_submit_provider_init(&fake_intent, &bus);
    intent_submit_provider_t intent_provider = fake_intent_submit_provider_get(&fake_intent);

    /* --- real SDL2 + LVGL bring-up --- */
    SDL_SetMainReady();   /* required alongside SDL_MAIN_HANDLED, above */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "ui_harness: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    lv_init();

    lv_display_t *disp = lv_sdl_window_create(UI_HARNESS_H_RES, UI_HARNESS_V_RES);
    if (disp == NULL) {
        fprintf(stderr, "ui_harness: lv_sdl_window_create failed\n");
        return 1;
    }
    lv_sdl_window_set_title(disp, "denon-spotify-controller -- ui_harness");
    lv_indev_t *mouse = lv_sdl_mouse_create();
    if (mouse == NULL) {
        fprintf(stderr, "ui_harness: lv_sdl_mouse_create failed\n");
        return 1;
    }

    ui_router_t router;
    if (!ui_router_create(&router, disp)) {
        fprintf(stderr, "ui_harness: ui_router_create failed\n");
        return 1;
    }

    /* Denon/Settings stay stub screens (Phase 6) -- Now Playing is the real
     * screen_now_playing.c, same as ui_app.c wires it on target. */
    screen_stub_cfg_t stub_denon    = { .label = "Denon",    .bg_rgb888 = 0x141417 };
    screen_stub_cfg_t stub_settings = { .label = "Settings", .bg_rgb888 = 0x1F1F24 };

    static view_model_now_playing_apply_t np_apply;   /* static: outlives main()'s
                                                           own stack frame for the
                                                           whole life of this process,
                                                           same reasoning as ui_app_t
                                                           on target */
    static screen_now_playing_ctx_t np_screen;
    np_screen.artwork_pixels = artwork_provider;
    np_screen.intent_submit  = view_model_now_playing_apply_intent_submit_provider(&np_apply);
    np_screen.intent_submit_raw = intent_provider;   /* w_volume_slider -- same raw/
                                                          unwrapped provider ui_app.c
                                                          wires on target, see its own
                                                          comment there */

    if (!ui_router_register_screen(&router, UI_SCREEN_NOW_PLAYING, screen_now_playing_get_interface(), &np_screen) ||
        !ui_router_register_screen(&router, UI_SCREEN_DENON,       screen_stub_get_interface(), &stub_denon) ||
        !ui_router_register_screen(&router, UI_SCREEN_SETTINGS,    screen_stub_get_interface(), &stub_settings)) {
        fprintf(stderr, "ui_harness: ui_router_register_screen failed\n");
        return 1;
    }

    if (!view_model_now_playing_apply_init(&np_apply, &state, &bus, &router, &np_screen,
                                              intent_provider, fake_time_source_get(&fts))) {
        fprintf(stderr, "ui_harness: view_model_now_playing_apply_init failed\n");
        return 1;
    }

    printf("ui_harness: fixture '%s' loaded. Tap a page-edge tab to switch screens; tap transport/volume/power controls to submit intents; close the window to quit.\n",
           ui_harness_fixture_name(fixture));

    /* No FreeRTOS task here -- LV_USE_OS is LV_OS_NONE (lv_conf.h), so this
     * IS the UI loop, driven by wall-clock ticks instead of an esp_timer.
     * LVGL's own SDL driver self-registers an internal timer that pumps
     * SDL_PollEvent (lv_sdl_window.c) -- this loop only needs to feed the
     * LVGL tick and call lv_timer_handler(), same shape as
     * UI_ARCHITECTURE.md §2's real task loop minus the RTOS-specific parts. */
    uint32_t last_tick_ms = SDL_GetTicks();
    for (;;) {
        uint32_t now_ms = SDL_GetTicks();
        fts.now_us += (int64_t)(now_ms - last_tick_ms) * 1000;   /* real wall-clock deltas,
                                                                      so progress interpolation
                                                                      (view_model_progress.c)
                                                                      actually animates here */
        lv_tick_inc(now_ms - last_tick_ms);
        last_tick_ms = now_ms;

        view_model_now_playing_apply_tick(&np_apply);

        uint32_t next_ms = lv_timer_handler();
        uint32_t delay_ms = (next_ms < UI_HARNESS_LOOP_MAX_DELAY_MS) ? next_ms : UI_HARNESS_LOOP_MAX_DELAY_MS;
        SDL_Delay(delay_ms);
    }
}
