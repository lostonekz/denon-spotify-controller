/*
 * See artwork_service.h. Target-only.
 */
#include "artwork_service.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"

#include "artwork_service_fetch.h"

static const char *TAG = "svc_artwork";

/* ARCHITECTURE.md §4 originally planned 6 KiB ("artwork", core 0, prio 3).
 * Real hardware (Tier 7) measured stack_hwm=484 bytes remaining out of that
 * 6 KiB on the FAILING fetch path (a failed fetch retries a full TLS
 * handshake, nested deeper than a clean fetch+decode) -- under this
 * project's 25% floor, and likely the real cause of a garbled MBEDTLS_ERR_
 * SSL_BAD_INPUT_DATA seen on the same path (a near-overflow corrupting
 * adjacent mbedTLS state, simpler than a real protocol violation). Raised
 * with real margin -- PSRAM-backed, negligible against ~3 MiB free. */
#define ARTWORK_TASK_STACK 16384
#define ARTWORK_TASK_PRIO  3
#define ARTWORK_TASK_CORE  0
#define ARTWORK_TASK_IDLE_DELAY_MS 100u

/* Real implementations -- task-owned (Phase 5 Tier -1, this file's top
 * comment): called only from svc_artwork_create() (before the task exists,
 * so nothing can race it) and from artwork_service_task()'s own loop while
 * draining a request. Never call these directly from another task -- use
 * svc_artwork_request_suspend/resume(), declared below. */
static bool artwork_service_resume_now(svc_artwork_t *a)
{
    if (!artwork_cache_alloc(&a->cache)) {
        return false;
    }
    a->jpeg_staging = heap_caps_malloc(ARTWORK_JPEG_STAGING_BYTES, MALLOC_CAP_SPIRAM);
    if (a->jpeg_staging == NULL) {
        artwork_cache_free(&a->cache);
        return false;
    }
    return true;
}

static bool artwork_service_suspend_now(svc_artwork_t *a)
{
    /* Critical ordering (artwork_cache.h's contract): publish FAILED BEFORE
     * freeing the backing buffer -- otherwise a stale handle in core_state
     * could lock() against already-freed memory. */
    artwork_service_publish_failed(a);

    artwork_cache_free(&a->cache);
    if (a->jpeg_staging != NULL) {
        heap_caps_free(a->jpeg_staging);
        a->jpeg_staging = NULL;
    }
    a->current_handle  = (artwork_handle_t){ 0 };
    a->last_track_id[0] = '\0';
    a->attempt_track_id[0] = '\0';   /* fresh attempt budget after resume, not whatever
                                         was left over from before the suspend */
    a->attempt_count       = 0;
    return true;
}

static void artwork_service_drain_lifecycle_request(svc_artwork_t *a)
{
    xSemaphoreTake(a->lifecycle_lock, portMAX_DELAY);
    artwork_lifecycle_request_t req = a->lifecycle_request;
    a->lifecycle_request = ARTWORK_LIFECYCLE_REQUEST_NONE;
    xSemaphoreGive(a->lifecycle_lock);

    if (req == ARTWORK_LIFECYCLE_REQUEST_SUSPEND) {
        artwork_service_suspend_now(a);
    } else if (req == ARTWORK_LIFECYCLE_REQUEST_RESUME) {
        if (!artwork_service_resume_now(a)) {
            ESP_LOGW(TAG, "resume failed (allocation) -- artwork stays suspended");
        }
    }
}

static void artwork_service_task(void *pv)
{
    svc_artwork_t *a = (svc_artwork_t *)pv;
    while (!a->task_should_stop) {
        /* Drained before this iteration's event poll. NOT sufficient on its
         * own, and this comment used to claim it was: it only makes the
         * suspend happen first, in the same iteration -- the fetch pass
         * still ran afterwards, into the buffer just freed. Its own guard is
         * what actually blocks that. */
        artwork_service_drain_lifecycle_request(a);

        bool any_event = false;
        event_t evt;
        while (core_eventbus_poll(a->bus, a->playback_sub, &evt)) {
            any_event = true;   /* drain fully -- re-read core_state once below, not once per event */
        }
        if (any_event) {
            artwork_service_handle_track_change(a);
        }
        vTaskDelay(pdMS_TO_TICKS(ARTWORK_TASK_IDLE_DELAY_MS));
    }
    xSemaphoreGive(a->stopped_sem);
    vTaskDelete(NULL);
}

void svc_artwork_request_suspend(svc_artwork_t *a)
{
    xSemaphoreTake(a->lifecycle_lock, portMAX_DELAY);
    a->lifecycle_request = ARTWORK_LIFECYCLE_REQUEST_SUSPEND;
    xSemaphoreGive(a->lifecycle_lock);
}

void svc_artwork_request_resume(svc_artwork_t *a)
{
    xSemaphoreTake(a->lifecycle_lock, portMAX_DELAY);
    a->lifecycle_request = ARTWORK_LIFECYCLE_REQUEST_RESUME;
    xSemaphoreGive(a->lifecycle_lock);
}

static bool artwork_pixel_lock_copy_trampoline(void *ctx, artwork_handle_t handle,
                                                 uint16_t *out_buf, size_t out_cap_bytes)
{
    svc_artwork_t *a = (svc_artwork_t *)ctx;
    return artwork_cache_lock_copy(&a->cache, handle, out_buf, out_cap_bytes);
}

artwork_pixel_provider_t svc_artwork_pixel_provider(svc_artwork_t *a)
{
    artwork_pixel_provider_t p = { .ctx = a, .lock_copy = artwork_pixel_lock_copy_trampoline };
    return p;
}

bool svc_artwork_create(svc_artwork_t *a, const svc_artwork_cfg_t *cfg)
{
    memset(a, 0, sizeof(*a));

    a->bus         = cfg->bus;
    a->state_store = cfg->state_store;
    a->clock       = cfg->clock;
    a->http        = cfg->http;
    a->http_policy = cfg->http_policy;

    if (!core_eventbus_subscribe(a->bus, TOPIC_PLAYBACK, a->playback_sub_storage,
                                    ARTWORK_PLAYBACK_SUB_DEPTH, &a->playback_sub)) {
        return false;
    }

    if (!artwork_service_resume_now(a)) {
        return false;
    }

    a->lifecycle_lock = xSemaphoreCreateMutex();
    if (a->lifecycle_lock == NULL) {
        artwork_service_suspend_now(a);
        return false;
    }

    a->stopped_sem = xSemaphoreCreateBinary();
    if (a->stopped_sem == NULL) {
        vSemaphoreDelete(a->lifecycle_lock);
        a->lifecycle_lock = NULL;
        artwork_service_suspend_now(a);
        return false;
    }

    a->task_should_stop = false;
    /* PSRAM-backed stack -- MEMORY.md §4: internal-SRAM margin is only
     * ~138 KiB, and this task's own fetch was caught failing a ~16.7 KiB
     * mbedTLS alloc against it. Same pattern already proven safe for
     * ui_app's stack (no WiFi/BT/ROM-call restriction on ESP32-S3). */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(artwork_service_task, "artwork", ARTWORK_TASK_STACK, a,
                                                       ARTWORK_TASK_PRIO, &a->task_handle, ARTWORK_TASK_CORE,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        vSemaphoreDelete(a->stopped_sem);
        a->stopped_sem = NULL;
        vSemaphoreDelete(a->lifecycle_lock);
        a->lifecycle_lock = NULL;
        artwork_service_suspend_now(a);
        return false;
    }
    return true;
}

void svc_artwork_teardown(void *handle)
{
    svc_artwork_t *a = (svc_artwork_t *)handle;

    if (a->task_handle != NULL) {
        a->task_should_stop = true;
        xSemaphoreTake(a->stopped_sem, portMAX_DELAY);
        a->task_handle = NULL;
    }
    if (a->stopped_sem != NULL) {
        vSemaphoreDelete(a->stopped_sem);
        a->stopped_sem = NULL;
    }
    if (a->lifecycle_lock != NULL) {
        vSemaphoreDelete(a->lifecycle_lock);
        a->lifecycle_lock = NULL;
    }
    core_eventbus_unsubscribe(a->bus, a->playback_sub);

    /* Same critical ordering as artwork_service_suspend_now(): publish
     * FAILED (clearing DOMAIN_ARTWORK's handle) BEFORE freeing the cache --
     * found missing here by review. Without this, a stale handle sits in
     * core_state referencing a cache about to be freed and re-allocated
     * with generation counters reset to 0, so a UI-side read after restart
     * could pass the generation check against a slot now holding a
     * different track's pixels. svc_artwork_restart() calls this before
     * svc_artwork_create(), so this is the only enforcement point. */
    artwork_service_publish_failed(a);
    artwork_cache_teardown(&a->cache);
    if (a->jpeg_staging != NULL) {
        heap_caps_free(a->jpeg_staging);
        a->jpeg_staging = NULL;
    }
}

bool svc_artwork_restart(void *restart_ctx, void **out_handle)
{
    svc_artwork_restart_ctx_t *rc = (svc_artwork_restart_ctx_t *)restart_ctx;

    svc_artwork_teardown(rc->a);
    if (!svc_artwork_create(rc->a, &rc->cfg)) {
        return false;
    }
    *out_handle = rc->a;
    return true;
}

service_desc_t svc_artwork_service_desc(svc_artwork_restart_ctx_t *restart_ctx)
{
    service_desc_t desc = {
        .id           = SVC_ID_ARTWORK,
        .name         = "svc_artwork",
        .requires     = SVC_BIT(SVC_ID_NET_WIFI),
        .restart      = RESTART_BACKOFF_THEN_DEGRADE,
        .max_restarts = 3,
        .degraded_ok  = true,
        .teardown     = svc_artwork_teardown,
        .restart_fn   = svc_artwork_restart,
        .restart_ctx  = restart_ctx,
    };
    return desc;
}
