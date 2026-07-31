#include "app_wiring.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "app_wiring_artwork.h"
#include "app_wiring_core.h"
#include "app_wiring_denon.h"
#include "app_wiring_spotify.h"
#include "app_wiring_ticks.h"
#include "fw_version_generated.h"
#include "ota_trust_anchor_dev.h"

static const char *TAG = "app_wiring";

/* --- the two non-portable primitives every core_* module needs injected --- */

static void real_lock(void *ctx)   { xSemaphoreTake((SemaphoreHandle_t)ctx, portMAX_DELAY); }
static void real_unlock(void *ctx) { xSemaphoreGive((SemaphoreHandle_t)ctx); }

static os_lock_if_t make_real_lock(SemaphoreHandle_t sem)
{
    os_lock_if_t l = { .lock = real_lock, .unlock = real_unlock, .ctx = sem };
    return l;
}

static int64_t real_now_us(void *ctx) { (void)ctx; return esp_timer_get_time(); }

static time_source_t real_clock(void)
{
    time_source_t t = { .now_us = real_now_us, .ctx = NULL };
    return t;
}

/* svc_scheduler tick trampolines (app_wiring owns scheduling; the services
 * themselves have no svc_scheduler dependency -- see svc_settings.h/
 * svc_diagnostics.h) live in app_wiring_ticks.c, split out purely for the
 * 400-line gate. */

#define SETTINGS_TICK_PERIOD_US    ((int64_t)1 * 1000000)
#define DIAGNOSTICS_TICK_PERIOD_US ((int64_t)5 * 1000000)
#define NET_TIME_TICK_PERIOD_US    ((int64_t)1 * 1000000)
#define SVC_OTA_TICK_PERIOD_US     ((int64_t)1 * 1000000)
#define HEALTH_GATE_TICK_PERIOD_US ((int64_t)1 * 1000000)
#define DENON_SETTINGS_RELAY_TICK_PERIOD_US ((int64_t)1 * 1000000)
/* Frequent enough not to undercut Phase 3's own sub-100ms feedback goal --
 * docs/ROADMAP.md Phase 3's plan picked ~20-50ms; 20ms chosen here. */
#define INTENT_DISPATCH_TICK_PERIOD_US ((int64_t)20 * 1000)
#define OTA_ARTWORK_RELAY_TICK_PERIOD_US ((int64_t)1 * 1000000)

/* docs/NETWORKING.md §1.6, verbatim -- "OTA...gets a different policy" is
 * not true yet in Phase 2 (only one real net_http caller exists), so this
 * one policy serves both the manifest check and the image download. */
static const http_policy_t OTA_HTTP_POLICY = {
    .connect_timeout_ms = 5000,
    .request_timeout_ms = 8000,
    .max_retries         = 2,
    .backoff_base_ms       = 500,
    .retry_on_5xx           = true,
    .honour_retry_after      = true,
};

/* Kconfig omits a `bool ... default n` symbol from sdkconfig.h entirely
 * when unset, rather than defining it 0 -- referencing
 * CONFIG_DSC_OTA_FORCE_CHECK_ON_BOOT directly as a plain C value would be
 * an undeclared identifier on any build that hasn't turned it on. */
#ifdef CONFIG_DSC_OTA_FORCE_CHECK_ON_BOOT
#define DSC_OTA_FORCE_CHECK_ON_BOOT_VAL true
#else
#define DSC_OTA_FORCE_CHECK_ON_BOOT_VAL false
#endif

static bool make_semaphore(SemaphoreHandle_t *out)
{
    *out = xSemaphoreCreateMutex();
    return *out != NULL;
}

bool app_wiring_create(app_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    time_source_t clock = real_clock();

    /* --- one-time ESP-IDF prerequisites for any esp_netif/esp_wifi/
     * esp_event use (net_wifi_create() itself owns esp_wifi_init() and its
     * own esp_netif_create_default_wifi_sta/ap() calls, but the netif
     * subsystem and default event loop are process-wide, not per-service) --- */

    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed");
        return false;
    }
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed");
        return false;
    }

    /* --- core --- */

    if (!make_semaphore(&ctx->bus_lock_sem)) {
        ESP_LOGE(TAG, "bus lock create failed");
        return false;
    }
    if (!app_wiring_create_bus(ctx, make_real_lock(ctx->bus_lock_sem))) {
        return false;   /* app_wiring_create_bus() already logged the reason */
    }

    /* --- intents (Phase 3): EVENTS_AND_STATE.md §5.1's single intent
     * queue -- see app_ctx.h's APP_INTENT_QUEUE_DEPTH comment for why this
     * is a dedicated FreeRTOS queue, not a core_eventbus topic. --- */
    ctx->intent_queue = xQueueCreate(APP_INTENT_QUEUE_DEPTH, sizeof(intent_t));
    if (ctx->intent_queue == NULL) {
        ESP_LOGE(TAG, "intent queue create failed");
        return false;
    }

    if (!make_semaphore(&ctx->state_lock_sem)) {
        ESP_LOGE(TAG, "state lock create failed");
        return false;
    }
    core_state_init(&ctx->state, make_real_lock(ctx->state_lock_sem), clock);

    /* --- storage first: every namespace-owning service needs one from it --- */

    if (!svc_storage_create(&ctx->storage)) {
        ESP_LOGE(TAG, "svc_storage_create failed");
        return false;
    }
    if (!svc_storage_open_namespace(&ctx->storage, "settings", KV_NS_PLAIN, &ctx->settings_kv)) {
        ESP_LOGE(TAG, "failed to open 'settings' namespace");
        return false;
    }
    if (!svc_storage_open_namespace(&ctx->storage, "diagnostics", KV_NS_PLAIN, &ctx->diagnostics_kv)) {
        ESP_LOGE(TAG, "failed to open 'diagnostics' namespace");
        return false;
    }
    /* PLATFORM.md §3.2: wifi.ssid/wifi.psk live here, alongside Phase 4's
     * Spotify secrets. KV_NS_ENCRYPTED is a documented no-op until the
     * production secure-init path exists (svc_storage.h) -- reserving the
     * option now costs nothing and avoids a signature change later. */
    if (!svc_storage_open_namespace(&ctx->storage, "secure", KV_NS_ENCRYPTED, &ctx->secure_kv)) {
        ESP_LOGE(TAG, "failed to open 'secure' namespace");
        return false;
    }
    if (!svc_storage_open_namespace(&ctx->storage, "ota", KV_NS_PLAIN, &ctx->ota_kv)) {
        ESP_LOGE(TAG, "failed to open 'ota' namespace");
        return false;
    }

    /* Health-gate condition (PLATFORM.md §2.2): "NVS mounted, settings
     * schema read" survived the update. Safe to report before
     * svc_supervisor_init()/svc_supervisor_health_gate_start() run below --
     * svc_supervisor_report_health_condition() only ever touches
     * ctx->supervisor.health, already zeroed by this function's own
     * memset(). */
    svc_supervisor_report_health_condition(&ctx->supervisor, HEALTH_COND_STORAGE_MOUNTED, true);

    if (!svc_scheduler_create(&ctx->scheduler, clock)) {
        ESP_LOGE(TAG, "svc_scheduler_create failed");
        return false;
    }

    if (!app_wiring_create_logging(ctx)) {
        return false;   /* app_wiring_create_logging() already logged the reason */
    }

    if (!svc_settings_create(&ctx->settings, ctx->settings_kv, &ctx->bus, clock)) {
        ESP_LOGE(TAG, "svc_settings_create failed");
        return false;
    }

    if (!svc_diagnostics_create(&ctx->diagnostics, &ctx->bus, ctx->diagnostics_kv, clock)) {
        ESP_LOGE(TAG, "svc_diagnostics_create failed");
        return false;
    }
    svc_diagnostics_check_coredump_on_boot(&ctx->diagnostics);

    /* --- net (Phase 2): net_wifi/net_time/net_http have no dependency on
     * each other (net has no self-edge, arch_rules.json) --- */

    net_wifi_cfg_t net_wifi_cfg = {
        .secure_kv      = ctx->secure_kv,
        .bus              = &ctx->bus,
        .state             = &ctx->state,
        .clock               = clock,
        .ap_ssid_prefix        = CONFIG_DSC_PROV_AP_SSID_PREFIX,
    };
    if (!net_wifi_create(&ctx->net_wifi, &net_wifi_cfg)) {
        ESP_LOGE(TAG, "net_wifi_create failed");
        return false;
    }

    net_time_cfg_t net_time_cfg = {
        .bus         = &ctx->bus,
        .sntp_server   = CONFIG_DSC_SNTP_SERVER,
    };
    if (!net_time_create(&ctx->net_time, &net_time_cfg)) {
        ESP_LOGE(TAG, "net_time_create failed");
        return false;
    }

    if (!net_http_create(&ctx->net_http)) {
        ESP_LOGE(TAG, "net_http_create failed");
        return false;
    }

    /* --- ota (Phase 2): depends on storage (its NVS namespace) and
     * net_http (its transport), both already up --- */

    svc_ota_cfg_t ota_cfg = {
        .bus                       = &ctx->bus,
        .http                       = &ctx->net_http,
        .kv                           = ctx->ota_kv,
        .http_policy                   = OTA_HTTP_POLICY,
        .manifest_url                   = CONFIG_DSC_OTA_MANIFEST_URL,
        .force_check_on_boot               = DSC_OTA_FORCE_CHECK_ON_BOOT_VAL,
        .anchors                             = DSC_DEV_TRUST_ANCHORS,
        .anchor_count                          = DSC_DEV_TRUST_ANCHOR_COUNT,
        .compiled_build                         = DSC_FW_BUILD,
        .compiled_security_version                = DSC_FW_SECURITY_VERSION,
    };
    if (!svc_ota_create(&ctx->svc_ota, &ota_cfg)) {
        ESP_LOGE(TAG, "svc_ota_create failed");
        return false;
    }

    /* --- denon (Phase 3): depends on net_http (its HTTP-fallback
     * transport) and an initial svc_settings snapshot, both already up --- */

    if (!app_wiring_create_denon(ctx, clock)) {
        return false;   /* app_wiring_create_denon() already logged the reason */
    }

    /* --- spotify (Phase 4): depends on net_http (its transport), net_time
     * (sync-gating, Phase 4 plan §2), and secure_kv (credentials), all
     * already up --- */

    if (!app_wiring_create_spotify(ctx, clock)) {
        return false;   /* app_wiring_create_spotify() already logged the reason */
    }

    /* --- artwork (Phase 4): depends on net_http (its transport) and
     * TOPIC_PLAYBACK (subscribed directly by svc_artwork itself, no
     * mediation needed -- see artwork_service.h) --- */

    if (!app_wiring_create_artwork(ctx, clock)) {
        return false;   /* app_wiring_create_artwork() already logged the reason */
    }

    /* --- lifecycle: collect every service's own descriptor, validate the
     * boot graph, mark everything READY --- */

    ctx->service_descs[0]  = SVC_STORAGE_SERVICE_DESC;
    ctx->service_descs[1]  = SVC_SCHEDULER_SERVICE_DESC;
    ctx->service_descs[2]  = SVC_LOGGING_SERVICE_DESC;
    ctx->service_descs[3]  = SVC_SETTINGS_SERVICE_DESC;
    ctx->service_descs[4]  = SVC_DIAGNOSTICS_SERVICE_DESC;
    ctx->service_descs[5]  = SVC_NET_WIFI_SERVICE_DESC;
    ctx->service_descs[6]  = SVC_NET_TIME_SERVICE_DESC;
    ctx->service_descs[7]  = SVC_NET_HTTP_SERVICE_DESC;
    ctx->service_descs[8]  = SVC_OTA_SERVICE_DESC;
    ctx->service_descs[9]  = svc_denon_service_desc(&ctx->svc_denon_restart_ctx);
    ctx->service_descs[10] = svc_spotify_service_desc(&ctx->svc_spotify_restart_ctx);
    ctx->service_descs[11] = svc_artwork_service_desc(&ctx->svc_artwork_restart_ctx);
    ctx->service_count = 12;

    if (!make_semaphore(&ctx->supervisor_lock_sem)) {
        ESP_LOGE(TAG, "supervisor lock create failed");
        return false;
    }
    svc_supervisor_init(&ctx->supervisor, ctx->service_descs, ctx->service_states,
                          ctx->service_count, make_real_lock(ctx->supervisor_lock_sem),
                          clock, esp_restart);

    /* LIFECYCLE.md §1.4: svc_supervisor owns the OTA health-gate verdict.
     * Started here (after the bus and supervisor both exist) so its
     * TOPIC_NET/TOPIC_TIME subscriptions are live before net_wifi/net_time
     * publish anything. */
    svc_supervisor_health_gate_start(&ctx->supervisor, &ctx->bus);

    if (!svc_supervisor_boot(&ctx->supervisor)) {
        ESP_LOGE(TAG, "boot graph is invalid (cycle or missing dependency)");
        return false;
    }

    /* --- periodic ticks, driven by the scheduler's shared worker task --- */

    if (!svc_scheduler_post_periodic(&ctx->scheduler, SETTINGS_TICK_PERIOD_US,
                                       settings_tick_trampoline, &ctx->settings,
                                       &ctx->settings_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule settings tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, DIAGNOSTICS_TICK_PERIOD_US,
                                       diagnostics_tick_trampoline, &ctx->diagnostics,
                                       &ctx->diagnostics_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule diagnostics tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, NET_TIME_TICK_PERIOD_US,
                                       net_time_tick_trampoline, &ctx->net_time,
                                       &ctx->net_time_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule net_time tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, SVC_OTA_TICK_PERIOD_US,
                                       svc_ota_tick_trampoline, &ctx->svc_ota,
                                       &ctx->svc_ota_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule svc_ota tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, HEALTH_GATE_TICK_PERIOD_US,
                                       health_gate_relay_tick_trampoline, ctx,
                                       &ctx->health_gate_relay_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule health-gate relay tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, DENON_SETTINGS_RELAY_TICK_PERIOD_US,
                                       denon_settings_relay_tick_trampoline, ctx,
                                       &ctx->denon_settings_relay_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule denon settings relay tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, INTENT_DISPATCH_TICK_PERIOD_US,
                                       intent_dispatch_tick_trampoline, ctx,
                                       &ctx->intent_dispatch_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule intent dispatch tick");
        return false;
    }
    if (!svc_scheduler_post_periodic(&ctx->scheduler, OTA_ARTWORK_RELAY_TICK_PERIOD_US,
                                       ota_artwork_relay_tick_trampoline, ctx,
                                       &ctx->ota_artwork_relay_tick_job)) {
        ESP_LOGE(TAG, "failed to schedule ota/artwork relay tick");
        return false;
    }

    /* Health-gate conditions this composition root can vouch for directly,
     * now that every task/service above has actually been created:
     * "boot reached app_start() completion" and "all tasks created and
     * feeding the watchdog" (PLATFORM.md §2.2). Real task-watchdog
     * registration is deferred -- too few long-running tasks exist yet in
     * Phase 2 to distinguish "feeding WDT" from "no trip" meaningfully
     * (same call the plan made merging those two conditions); revisit once
     * Phase 3+ adds denon/spotify/artwork. "Panel initialised, UI
     * rendered, touch enumerated" is hardcoded true -- no hal_display/ui
     * until Phase 5, a documented gap, not a silent cut. */
#ifndef CONFIG_DSC_TEST_FORCE_HEALTH_GATE_TIMEOUT
    svc_supervisor_report_health_condition(&ctx->supervisor, HEALTH_COND_BOOT_COMPLETE, true);
#else
    /* docs/ROADMAP.md Tier 7's rollback proof: withhold this condition so
     * the gate can only ever TIMED_OUT, never PASS. Kconfig-gated, and
     * tools/check_release.py refuses a production profile with this set. */
    ESP_LOGW(TAG, "DSC_TEST_FORCE_HEALTH_GATE_TIMEOUT set -- health gate will never pass");
#endif
    svc_supervisor_report_health_condition(&ctx->supervisor, HEALTH_COND_TASKS_OK, true);
    svc_supervisor_report_health_condition(&ctx->supervisor, HEALTH_COND_PANEL_UI, true);

    ESP_LOGI(TAG, "app_wiring: %d service(s) ready", (int)ctx->service_count);
    return true;
}

bool app_wiring_submit_intent(app_ctx_t *ctx, const intent_t *intent)
{
    /* EVENTS_AND_STATE.md §5.3's documented policy for this queue: drop
     * newest + count it, not block the submitting task or evict an
     * already-queued intent. */
    if (xQueueSend(ctx->intent_queue, intent, 0) != pdTRUE) {
        ctx->intent_drop_count++;
        return false;
    }
    return true;
}
