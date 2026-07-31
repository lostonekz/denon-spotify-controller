/*
 * See app_wiring_spotify.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_spotify.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "app_wiring";

/* accounts.spotify.com/api.spotify.com are internet hosts, not LAN --
 * distinct from Denon's LAN-tuned policy. NETWORKING.md §1.6: "Spotify,
 * OTA, and artwork each get different policies," even where the actual
 * numbers start out the same as OTA_HTTP_POLICY's. */
static const http_policy_t SPOTIFY_HTTP_POLICY = {
    .connect_timeout_ms = 5000,
    .request_timeout_ms = 8000,
    .max_retries         = 2,
    .backoff_base_ms       = 500,
    .retry_on_5xx           = true,
    .honour_retry_after      = true,
};

bool app_wiring_create_spotify(app_ctx_t *ctx, time_source_t clock)
{
    char client_id[SPOTIFY_CLIENT_ID_MAX] = { 0 };
    if (!spotify_client_id_load(&ctx->secure_kv, client_id) && CONFIG_DSC_SPOTIFY_CLIENT_ID[0] != '\0') {
        strncpy(client_id, CONFIG_DSC_SPOTIFY_CLIENT_ID, sizeof(client_id) - 1);
        spotify_client_id_save(&ctx->secure_kv, client_id);
    }

    char redirect_uri[SPOTIFY_REDIRECT_URI_MAX] = { 0 };
    if (!spotify_redirect_uri_load(&ctx->secure_kv, redirect_uri) &&
        CONFIG_DSC_SPOTIFY_REDIRECT_URI[0] != '\0') {
        strncpy(redirect_uri, CONFIG_DSC_SPOTIFY_REDIRECT_URI, sizeof(redirect_uri) - 1);
        spotify_redirect_uri_save(&ctx->secure_kv, redirect_uri);
    }

    spotify_avr_pin_t avr_pin = { 0 };
    spotify_avr_pin_load(&ctx->secure_kv, &avr_pin);   /* ok to fail -- stays unset */

    spotify_session_credentials_t creds = { 0 };
    bool has_creds = spotify_credentials_load(&ctx->secure_kv, &creds);

    svc_spotify_cfg_t spotify_cfg = {
        .http        = &ctx->net_http,
        .time          = &ctx->net_time,
        .bus             = &ctx->bus,
        .state_store       = &ctx->state,
        .clock                = clock,
        .http_policy             = SPOTIFY_HTTP_POLICY,
        .secure_kv                 = &ctx->secure_kv,
        .state_cfg = {
            .has_stored_creds     = has_creds,
            .stored_creds           = creds,
            .stored_avr_pin           = avr_pin,
            .session_fsm_log           = spotify_session_fsm_transition_logger,
            .session_fsm_log_ctx         = NULL,
        },
    };
    strncpy(spotify_cfg.state_cfg.client_id, client_id, sizeof(spotify_cfg.state_cfg.client_id) - 1);
    strncpy(spotify_cfg.state_cfg.redirect_uri, redirect_uri, sizeof(spotify_cfg.state_cfg.redirect_uri) - 1);

    if (!svc_spotify_create(&ctx->svc_spotify, &spotify_cfg)) {
        ESP_LOGE(TAG, "svc_spotify_create failed");
        return false;
    }
    /* Retained for svc_spotify_restart() -- same reasoning as
     * app_wiring_denon.c's own restart ctx. */
    ctx->svc_spotify_restart_ctx.s   = &ctx->svc_spotify;
    ctx->svc_spotify_restart_ctx.cfg = spotify_cfg;
    return true;
}
