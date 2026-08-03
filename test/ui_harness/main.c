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
#include "fake_system_apply.h"
#include "fake_time_source.h"
#include "fixtures.h"
#include "screen_connection.h"
#include "screen_denon.h"
#include "screen_now_playing.h"
#include "screen_system.h"
#include "ui_router.h"
#include "view_model_connection_apply.h"
#include "view_model_denon_apply.h"
#include "view_model_now_playing_apply.h"

#define UI_HARNESS_H_RES 480
#define UI_HARNESS_V_RES 480
#define UI_HARNESS_LOOP_MAX_DELAY_MS 16

/* Depths/policies mirror main/app_wiring_core.c's real values for these
 * same topics exactly, not arbitrary numbers. Bumped from 8 for the two
 * new screens' own subscriptions (Connection's own TOPIC_DENON sub, and
 * fake_system_apply's TOPIC_NET/TOPIC_DENON/TOPIC_SPOTIFY subs). */
#define UI_HARNESS_BUS_MAX_SUBS 16

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
        { .topic = TOPIC_NET,        .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
        { .topic = TOPIC_SPOTIFY,    .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST, .on_release = NULL, .release_ctx = NULL },
    };
    core_eventbus_t bus;
    core_eventbus_init(&bus, bus_subs, UI_HARNESS_BUS_MAX_SUBS, bus_policies, 7, fake_lock_get());

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

    /* Now Playing, Denon, Connection, and System are all real screens,
     * registered in the same order and with the same providers ui_app.c
     * wires on target. */
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

    static view_model_denon_apply_t denon_apply;   /* static for the same reason as
                                                        np_apply above */
    static screen_denon_ctx_t denon_screen;
    denon_screen.intent_submit_raw = intent_provider;   /* every control on the Receiver
                                                             screen is Denon-domain, so the
                                                             raw provider is the only one it
                                                             needs -- screen_denon.h's own
                                                             comment, matching ui_app.c */

    static view_model_connection_apply_t connection_apply;
    static screen_connection_ctx_t connection_screen;
    connection_screen.intent_submit_raw = intent_provider;

    /* System is fully read-only (no intent_submit field) and its real
     * apply-glue (view_model_system_apply.c) calls 3 real ESP-IDF APIs
     * directly, so it can't link into this host harness at all --
     * fake_system_apply.c stands in, reusing the same real, portable
     * view_model_system_build() with fixed fake values for those 3 calls. */
    static fake_system_apply_t system_apply;
    static screen_system_ctx_t system_screen;

    if (!ui_router_register_screen(&router, UI_SCREEN_NOW_PLAYING, screen_now_playing_get_interface(), &np_screen) ||
        !ui_router_register_screen(&router, UI_SCREEN_DENON,       screen_denon_get_interface(), &denon_screen) ||
        !ui_router_register_screen(&router, UI_SCREEN_CONNECTION,  screen_connection_get_interface(), &connection_screen) ||
        !ui_router_register_screen(&router, UI_SCREEN_SYSTEM,      screen_system_get_interface(), &system_screen)) {
        fprintf(stderr, "ui_harness: ui_router_register_screen failed\n");
        return 1;
    }

    if (!view_model_now_playing_apply_init(&np_apply, &state, &bus, &router, &np_screen,
                                              intent_provider, fake_time_source_get(&fts))) {
        fprintf(stderr, "ui_harness: view_model_now_playing_apply_init failed\n");
        return 1;
    }

    if (!view_model_denon_apply_init(&denon_apply, &state, &bus, &router)) {
        fprintf(stderr, "ui_harness: view_model_denon_apply_init failed\n");
        return 1;
    }

    if (!view_model_connection_apply_init(&connection_apply, &state, &bus, &router)) {
        fprintf(stderr, "ui_harness: view_model_connection_apply_init failed\n");
        return 1;
    }

    if (!fake_system_apply_init(&system_apply, &state, &bus, &router, fake_time_source_get(&fts))) {
        fprintf(stderr, "ui_harness: fake_system_apply_init failed\n");
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

        /* Both apply ticks every iteration, same as ui_app_task does on
         * target. Each drains its own subscriptions unconditionally (so no
         * event ring can fill while its screen is off-view) and then calls
         * ui_router_update(), which itself no-ops for whichever screen is
         * not currently active. */
        view_model_now_playing_apply_tick(&np_apply);
        view_model_denon_apply_tick(&denon_apply);
        view_model_connection_apply_tick(&connection_apply);
        fake_system_apply_tick(&system_apply);

        uint32_t next_ms = lv_timer_handler();
        uint32_t delay_ms = (next_ms < UI_HARNESS_LOOP_MAX_DELAY_MS) ? next_ms : UI_HARNESS_LOOP_MAX_DELAY_MS;
        SDL_Delay(delay_ms);
    }
}
