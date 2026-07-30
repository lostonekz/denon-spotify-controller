#include "app_wiring.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

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

/* --- svc_scheduler tick trampolines (app_wiring owns scheduling; the
 * services themselves have no svc_scheduler dependency -- see
 * svc_settings.h/svc_diagnostics.h) --- */

static void settings_tick_trampoline(void *ctx)    { svc_settings_tick((svc_settings_t *)ctx); }
static void diagnostics_tick_trampoline(void *ctx) { svc_diagnostics_report_tick((svc_diagnostics_t *)ctx); }

#define SETTINGS_TICK_PERIOD_US    ((int64_t)1 * 1000000)
#define DIAGNOSTICS_TICK_PERIOD_US ((int64_t)5 * 1000000)

static bool make_semaphore(SemaphoreHandle_t *out)
{
    *out = xSemaphoreCreateMutex();
    return *out != NULL;
}

bool app_wiring_create(app_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    time_source_t clock = real_clock();

    /* --- core --- */

    if (!make_semaphore(&ctx->bus_lock_sem)) {
        ESP_LOGE(TAG, "bus lock create failed");
        return false;
    }
    ctx->bus_topic_policies[0] = (topic_policy_t){
        .topic = TOPIC_SETTINGS, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[1] = (topic_policy_t){
        .topic = TOPIC_DIAG, .depth = 4, .on_full = EVENT_OVERFLOW_DROP_NEWEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    core_eventbus_init(&ctx->bus, ctx->bus_subs, APP_EVENTBUS_MAX_SUBS,
                        ctx->bus_topic_policies, APP_EVENTBUS_TOPIC_POLICIES,
                        make_real_lock(ctx->bus_lock_sem));

    if (!make_semaphore(&ctx->state_lock_sem)) {
        ESP_LOGE(TAG, "state lock create failed");
        return false;
    }
    core_state_init(&ctx->state, make_real_lock(ctx->state_lock_sem), clock);

    /* --- storage first: settings/diagnostics both need a namespace from it --- */

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

    if (!svc_scheduler_create(&ctx->scheduler, clock)) {
        ESP_LOGE(TAG, "svc_scheduler_create failed");
        return false;
    }

    /* Empty ruleset: no secret-bearing subsystem exists yet in Phase 1
     * (svc_logging_redact.c's mechanism is proven against a synthetic
     * secret in its own host test). */
    if (!svc_logging_create(&ctx->logging, NULL, 0)) {
        ESP_LOGE(TAG, "svc_logging_create failed");
        return false;
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

    /* --- lifecycle: collect every service's own descriptor, validate the
     * boot graph, mark everything READY --- */

    ctx->service_descs[0] = SVC_STORAGE_SERVICE_DESC;
    ctx->service_descs[1] = SVC_SCHEDULER_SERVICE_DESC;
    ctx->service_descs[2] = SVC_LOGGING_SERVICE_DESC;
    ctx->service_descs[3] = SVC_SETTINGS_SERVICE_DESC;
    ctx->service_descs[4] = SVC_DIAGNOSTICS_SERVICE_DESC;
    ctx->service_count = 5;

    if (!make_semaphore(&ctx->supervisor_lock_sem)) {
        ESP_LOGE(TAG, "supervisor lock create failed");
        return false;
    }
    svc_supervisor_init(&ctx->supervisor, ctx->service_descs, ctx->service_states,
                          ctx->service_count, make_real_lock(ctx->supervisor_lock_sem),
                          clock, esp_restart);

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

    ESP_LOGI(TAG, "app_wiring: %d service(s) ready", (int)ctx->service_count);
    return true;
}
