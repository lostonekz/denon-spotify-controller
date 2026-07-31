/*
 * See app_wiring_artwork.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_artwork.h"

#include "esp_log.h"

static const char *TAG = "app_wiring";

/* i.scdn.co, a third internet host distinct from Spotify's own API host --
 * NETWORKING.md §1.6: "Spotify, OTA, and artwork each get different
 * policies." Slightly shorter timeouts than SPOTIFY_HTTP_POLICY: an
 * artwork fetch that's genuinely stuck is better abandoned quickly (the
 * next track change will just try again) than held open competing for one
 * of the arbiter's 2 TLS slots. */
static const http_policy_t ARTWORK_HTTP_POLICY = {
    .connect_timeout_ms = 4000,
    .request_timeout_ms = 6000,
    .max_retries         = 1,
    .backoff_base_ms       = 500,
    .retry_on_5xx           = true,
    .honour_retry_after      = true,
};

bool app_wiring_create_artwork(app_ctx_t *ctx, time_source_t clock)
{
    svc_artwork_cfg_t artwork_cfg = {
        .bus         = &ctx->bus,
        .state_store   = &ctx->state,
        .clock            = clock,
        .http               = &ctx->net_http,
        .http_policy          = ARTWORK_HTTP_POLICY,
    };

    if (!svc_artwork_create(&ctx->svc_artwork, &artwork_cfg)) {
        ESP_LOGE(TAG, "svc_artwork_create failed");
        return false;
    }
    ctx->svc_artwork_restart_ctx.a   = &ctx->svc_artwork;
    ctx->svc_artwork_restart_ctx.cfg = artwork_cfg;
    return true;
}
