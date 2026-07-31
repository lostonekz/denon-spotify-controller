/*
 * See app_wiring_denon.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_denon.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "app_wiring";

/* AVR is LAN-local, not internet -- deliberately shorter timeouts than
 * app_wiring.c's own OTA_HTTP_POLICY. No 5xx-retry/Retry-After handling:
 * Denon's HTTP interface doesn't meaningfully use either (docs/RISKS.md
 * R5, undocumented vendor interface). */
static const http_policy_t DENON_HTTP_POLICY = {
    .connect_timeout_ms = 2000,
    .request_timeout_ms = 3000,
    .max_retries         = 1,
    .backoff_base_ms       = 200,
    .retry_on_5xx           = false,
    .honour_retry_after      = false,
};

/* svc_settings.h's denon_transport_pref_t and denon_service_state.h's
 * denon_link_transport_pref_t are two independently-declared enums with
 * the same 3 values in the same order (svc_denon can't #include
 * svc_settings.h to share one -- no svc->svc edge) -- an explicit
 * conversion, not a raw cast, so a future divergence between the two is a
 * compile-time switch-exhaustiveness issue to notice, not a silent
 * numeric coincidence. */
static denon_link_transport_pref_t denon_transport_pref_from_settings(uint8_t v)
{
    switch ((denon_transport_pref_t)v) {
    case DENON_TRANSPORT_TELNET: return DENON_LINK_TRANSPORT_TELNET;
    case DENON_TRANSPORT_HTTP:   return DENON_LINK_TRANSPORT_HTTP;
    case DENON_TRANSPORT_AUTO:
    default:                     return DENON_LINK_TRANSPORT_AUTO;
    }
}

bool app_wiring_create_denon(app_ctx_t *ctx, time_source_t clock)
{
    char denon_host[SETTING_STR_MAX] = { 0 };
    settings_get_str(&ctx->settings, SETTING_DENON_HOST, denon_host);

    uint8_t denon_transport_pref_u8 = (uint8_t)DENON_TRANSPORT_AUTO;
    settings_get_u8(&ctx->settings, SETTING_DENON_TRANSPORT_PREF, &denon_transport_pref_u8);

    uint8_t denon_volume_limit_u8 = 70;   /* PLATFORM.md §3.4's own default, if the read fails */
    settings_get_u8(&ctx->settings, SETTING_VOLUME_LIMIT_MAX, &denon_volume_limit_u8);

    svc_denon_cfg_t denon_cfg = {
        .http        = &ctx->net_http,
        .bus           = &ctx->bus,
        .state_store     = &ctx->state,
        .clock              = clock,
        .http_policy          = DENON_HTTP_POLICY,
        .state_cfg = {
            .transport_pref       = denon_transport_pref_from_settings(denon_transport_pref_u8),
            .volume_limit_half_db   = (uint16_t)denon_volume_limit_u8,
            .link_fsm_log             = denon_link_fsm_transition_logger,
            .link_fsm_log_ctx           = NULL,
        },
    };
    strncpy(denon_cfg.state_cfg.initial_host, denon_host, sizeof(denon_cfg.state_cfg.initial_host) - 1);

    if (!svc_denon_create(&ctx->svc_denon, &denon_cfg)) {
        ESP_LOGE(TAG, "svc_denon_create failed");
        return false;
    }
    /* Retained for svc_denon_restart() -- a restart recreates svc_denon
     * from exactly this cfg. The pointers inside it (http/bus/state_store)
     * point at ctx's own fields, valid for the device's whole lifetime. */
    ctx->svc_denon_restart_ctx.d   = &ctx->svc_denon;
    ctx->svc_denon_restart_ctx.cfg = denon_cfg;
    return true;
}
