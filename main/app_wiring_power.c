/*
 * See app_wiring_power.h.
 */
#include "app_wiring_power.h"

#include "esp_log.h"

#include "app_wiring_ticks.h"
#include "bsp_board.h"
#include "hal_backlight.h"

static const char *TAG = "app_wiring";

/* Kconfig gives minutes because that is the unit a human reasons about a
 * screen timeout in; svc_power wants milliseconds. 0 stays 0 and means
 * "never sleep" all the way down (svc_power_cfg_t.sleep_after_ms). */
#define POWER_SLEEP_AFTER_MS ((uint32_t)CONFIG_DSC_SCREEN_SLEEP_AFTER_MIN * 60u * 1000u)

/* The tick: feed svc_power the one input it cannot reach itself, then drain
 * whatever it decided and actuate.
 *
 * Both halves in one job rather than two, deliberately. svc_power publishes
 * synchronously inside its tick, so draining immediately afterwards makes
 * the decision and the backlight write land in the same job with no
 * inter-job latency -- which matters here because this period IS the
 * touch-to-wake latency (see POWER_TICK_PERIOD_US). Two jobs at the same
 * period could just as easily run in the unhelpful order.
 *
 * The drain still goes through TOPIC_POWER rather than reading svc_power's
 * mode directly, so that any other future producer of a power change (an
 * INTENT_DOMAIN_POWER, which EVENTS_AND_STATE.md §5.1 lists as declared-
 * but-dead) actuates through this same path instead of needing its own.
 */
static void power_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;

    svc_power_tick(&a->svc_power, ui_app_touch_idle_ms(&a->ui_app));

    event_t evt;
    while (core_eventbus_poll(&a->bus, a->power_sub, &evt)) {
        if (evt.topic != TOPIC_POWER || evt.u.power.kind != POWER_EVT_STATE_CHANGED) {
            continue;
        }
        bool asleep = (evt.u.power.power_state == (uint8_t)POWER_MODE_ASLEEP);

        /* Told before the backlight goes off and after it comes back on is
         * not a distinction that matters -- the UI only uses this to decide
         * whether to swallow the next touch, and both orderings leave that
         * flag correct by the time any human can react. */
        ui_app_set_display_asleep(&a->ui_app, asleep);

        /* The one hal call in the whole feature. A failed I2C write to the
         * CH32V003 is logged and dropped rather than retried: the next
         * state change will write again, and a backlight stuck on is a
         * cosmetic fault, not a reason to fail a tick. */
        if (hal_backlight_set_enabled(bsp_board_io_expander(), !asleep) != ESP_OK) {
            ESP_LOGW(TAG, "backlight %s failed", asleep ? "off" : "on");
        }
    }
}

bool app_wiring_create_power(app_ctx_t *ctx, time_source_t clock)
{
    svc_power_cfg_t cfg = {
        .state           = &ctx->state,
        .bus              = &ctx->bus,
        .clock             = clock,
        .sleep_after_ms     = POWER_SLEEP_AFTER_MS,
    };
    if (!svc_power_create(&ctx->svc_power, &cfg)) {
        ESP_LOGE(TAG, "svc_power_create failed");
        return false;
    }

    if (!core_eventbus_subscribe(&ctx->bus, TOPIC_POWER, ctx->power_sub_storage,
                                   sizeof(ctx->power_sub_storage) / sizeof(ctx->power_sub_storage[0]),
                                   &ctx->power_sub)) {
        ESP_LOGE(TAG, "power subscription failed");
        return false;
    }

    if (!svc_scheduler_post_periodic(&ctx->scheduler, POWER_TICK_PERIOD_US,
                                       power_tick_trampoline, ctx,
                                       &ctx->power_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule power tick");
        return false;
    }

    if (POWER_SLEEP_AFTER_MS == 0u) {
        ESP_LOGI(TAG, "screen sleep disabled (CONFIG_DSC_SCREEN_SLEEP_AFTER_MIN=0)");
    } else {
        ESP_LOGI(TAG, "screen sleeps after %d min idle", CONFIG_DSC_SCREEN_SLEEP_AFTER_MIN);
    }
    return true;
}
