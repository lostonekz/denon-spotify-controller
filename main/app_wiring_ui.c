/*
 * See app_wiring_ui.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_ui.h"

#include "esp_log.h"

#include "app_wiring.h"
#include "bsp_board.h"
#include "hal_backlight.h"
#include "intent_types.h"

static const char *TAG = "app_wiring";

/* The one real caller for a real, non-zero intent_t.seq -- confirmed by
 * reading every existing intent_t construction site (debug_console_
 * spotify_playback.c, app_wiring_volume.c): all of them leave seq at 0,
 * since none needed to watch for the resulting TOPIC_INTENT_ACK.
 * view_model_now_playing_apply.c's pending/reconciliation flow is the
 * first caller that does (core_types/intent_submit_types.h's own top
 * comment). A plain module-static counter, not part of app_ctx_t: every
 * submission from `ui` funnels through this one translation unit, and
 * uniqueness only ever needs to hold among THIS module's own outstanding,
 * watched intents -- not globally across every intent source in the
 * system (debug-console-submitted intents never watch for an ACK at all). */
static uint32_t s_next_ui_intent_seq = 0;

static bool ui_intent_submit(void *ctx, intent_t *intent)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    intent->seq = ++s_next_ui_intent_seq;   /* starts at 1 -- 0 stays reserved for
                                                "never submitted" (a zero-initialized
                                                intent_t), never a real assigned seq */
    return app_wiring_submit_intent(a, intent);
}

bool app_wiring_create_ui(app_ctx_t *ctx, time_source_t clock)
{
    if (bsp_board_init() != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed");
        return false;
    }

    if (hal_display_init(&ctx->hal_display, bsp_board_io_expander()) != ESP_OK) {
        ESP_LOGE(TAG, "hal_display_init failed");
        return false;
    }

    if (hal_touch_init(&ctx->hal_touch, bsp_board_i2c_bus(), bsp_board_io_expander()) != ESP_OK) {
        ESP_LOGE(TAG, "hal_touch_init failed");
        return false;
    }

    if (hal_backlight_init(bsp_board_io_expander()) != ESP_OK) {
        ESP_LOGE(TAG, "hal_backlight_init failed");
        return false;
    }

    ui_app_cfg_t ui_cfg = {
        .hal_disp       = &ctx->hal_display,
        .hal_touch      = &ctx->hal_touch,
        .artwork_pixels = svc_artwork_pixel_provider(&ctx->svc_artwork),
        .intent_submit  = { .ctx = ctx, .submit = ui_intent_submit },
        .state             = &ctx->state,
        .bus                 = &ctx->bus,
        .clock                  = clock,
        /* Passed in rather than read in components/ui, which has no business
         * including sdkconfig.h -- the pair overlay only needs it to build
         * the URL its QR encodes. */
        .pair_http_port  = (uint16_t)CONFIG_DSC_PAIR_HTTP_PORT,
    };
    if (!ui_app_create(&ctx->ui_app, &ui_cfg)) {
        ESP_LOGE(TAG, "ui_app_create failed");
        return false;
    }

    return true;
}
