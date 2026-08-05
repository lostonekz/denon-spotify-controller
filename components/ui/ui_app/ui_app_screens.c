#include "ui_app_screens.h"

#include "esp_log.h"

static const char *TAG = "ui_app_screens";

/* Both directions of the pair overlay's presentation, kept here so neither
 * screen has to know about the router: screen_system taps to open,
 * screen_pair calls back to close. */
static void on_pair_overlay_open(void *ctx)
{
    ui_router_present_overlay((ui_router_t *)ctx);
}

static void on_pair_overlay_close(void *ctx)
{
    ui_router_dismiss_overlay((ui_router_t *)ctx);
}

bool ui_app_screens_create(ui_app_t *app, const ui_app_cfg_t *cfg)
{
    /* Now Playing's own screen_now_playing_ctx_t.intent_submit must be the
     * WRAPPED provider (view_model_now_playing_apply's own), not cfg's raw
     * one directly -- that wrapper is the one place that observes every
     * submit() call to start pending/ACK tracking (view_model_now_playing_
     * apply.h's own top comment). Safe to construct before np_apply_init()
     * runs below: it only reads app->np_apply's fields at call time, on a
     * real tap, well after this function returns. */
    app->np_screen.artwork_pixels = cfg->artwork_pixels;
    app->np_screen.intent_submit  = view_model_now_playing_apply_intent_submit_provider(&app->np_apply);
    /* w_volume_slider uses THIS raw provider, not the wrapped one above
     * (w_power_glyph did too, before it and its header control were
     * removed -- screen_now_playing.h's own top comment) -- a real bug
     * found during the Art-forward redesign's own planning: the wrapped
     * provider's wrapped_submit() sets a single screen-wide pending flag
     * on every successful submit regardless of domain, which w_transport_
     * set_state() reads to dim/disable the transport row. Denon volume is
     * a different domain entirely and gives near-instant telnet-echoed
     * feedback, unlike Spotify's poll-confirmed round trip that pending/toast
     * machinery exists for -- routing it through that provider would visibly
     * (and incorrectly) dim the transport row on every volume drag. */
    app->np_screen.intent_submit_raw = cfg->intent_submit;

    /* Every control on the Receiver/Connection screens submits Denon- or
     * settings-domain intents with a near-instant round trip (w_power_row/
     * w_power_glyph, w_volume_slider, w_volume_cap_slider) -- the same
     * reasoning above for np_screen's own raw provider applies here too, so
     * neither screen gets a wrapped variant at all (screen_denon.h's own
     * comment). */
    app->denon_screen.intent_submit_raw      = cfg->intent_submit;
    app->connection_screen.intent_submit_raw = cfg->intent_submit;

    /* Same raw provider, same reasoning: INTENT_DOMAIN_PAIRING has nothing to
     * do with Now Playing's transport row, and routing it through the wrapped
     * provider would dim that row every time this overlay opened.
     * screen_pair is handed a close callback rather than the router itself,
     * so presentation stays owned by whoever owns the router. */
    app->pair_screen.intent_submit_raw = cfg->intent_submit;
    app->pair_screen.on_close           = on_pair_overlay_close;
    app->pair_screen.on_close_ctx      = &app->router;

    /* The System screen's Spotify row is the only way in. Set before
     * registration, since screen_system's create() wires the callback. */
    app->system_screen.on_spotify_activate     = on_pair_overlay_open;
    app->system_screen.on_spotify_activate_ctx = &app->router;

    if (!ui_router_register_screen(&app->router, UI_SCREEN_NOW_PLAYING, screen_now_playing_get_interface(), &app->np_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_DENON,       screen_denon_get_interface(), &app->denon_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_CONNECTION,  screen_connection_get_interface(), &app->connection_screen) ||
        !ui_router_register_screen(&app->router, UI_SCREEN_SYSTEM,      screen_system_get_interface(), &app->system_screen)) {
        ESP_LOGE(TAG, "ui_router_register_screen failed");
        return false;
    }

    /* AFTER all four screens, which ui_router_register_overlay() requires:
     * register_screen() re-parks both page-edge tabs at child index -1 on
     * every call, and an overlay registered earlier would end up below them. */
    if (!ui_router_register_overlay(&app->router, screen_pair_get_interface(), &app->pair_screen)) {
        ESP_LOGE(TAG, "ui_router_register_overlay failed");
        return false;
    }

    if (!view_model_now_playing_apply_init(&app->np_apply, cfg->state, cfg->bus, &app->router,
                                              &app->np_screen, cfg->intent_submit, cfg->clock)) {
        ESP_LOGE(TAG, "view_model_now_playing_apply_init failed");
        return false;
    }

    if (!view_model_denon_apply_init(&app->denon_apply, cfg->state, cfg->bus, &app->router)) {
        ESP_LOGE(TAG, "view_model_denon_apply_init failed");
        return false;
    }

    if (!view_model_connection_apply_init(&app->connection_apply, cfg->state, cfg->bus, &app->router)) {
        ESP_LOGE(TAG, "view_model_connection_apply_init failed");
        return false;
    }

    if (!view_model_system_apply_init(&app->system_apply, cfg->state, cfg->bus, &app->router, cfg->clock)) {
        ESP_LOGE(TAG, "view_model_system_apply_init failed");
        return false;
    }

    if (!view_model_pair_apply_init(&app->pair_apply, cfg->state, cfg->bus, &app->router, cfg->pair_http_port)) {
        ESP_LOGE(TAG, "view_model_pair_apply_init failed");
        return false;
    }

    return true;
}

void ui_app_screens_tick(ui_app_t *app)
{
    view_model_now_playing_apply_tick(&app->np_apply);
    view_model_denon_apply_tick(&app->denon_apply);
    view_model_connection_apply_tick(&app->connection_apply);
    view_model_system_apply_tick(&app->system_apply);
    view_model_pair_apply_tick(&app->pair_apply);
}
