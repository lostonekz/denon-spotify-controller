/*
 * See artwork_service.h. Target-only.
 */
#include "artwork_service.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"

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

static void publish_failed(svc_artwork_t *a)
{
    artwork_state_t st = { 0 };
    core_state_write(a->state_store, DOMAIN_ARTWORK, LINK_STATUS_OK, &st, sizeof(st));

    event_t evt = { .topic = TOPIC_ARTWORK };
    evt.u.artwork.kind = ARTWORK_EVT_FAILED;
    core_eventbus_publish(a->bus, &evt);
}

static void mark_track_done(svc_artwork_t *a, const char *track_id)
{
    strncpy(a->last_track_id, track_id, sizeof(a->last_track_id) - 1);
    a->last_track_id[sizeof(a->last_track_id) - 1] = '\0';
}

static void handle_track_change(svc_artwork_t *a)
{
    playback_state_t pb;
    if (!core_state_read(a->state_store, DOMAIN_PLAYBACK, &pb, sizeof(pb))) {
        return;
    }

    if (pb.track_id[0] == '\0') {
        /* Nothing playing anywhere. Tier -1: svc_spotify now publishes this
         * transition (NO_ACTIVE_DEVICE fix) -- previously a held handle
         * stuck on screen forever after playback stopped, since this
         * function never learned. Release+clear once, not every poll. */
        if (a->current_handle.generation != 0) {
            artwork_cache_release(&a->cache, a->current_handle);
            a->current_handle = (artwork_handle_t){ 0 };
        }
        if (a->last_track_id[0] != '\0') {
            a->last_track_id[0] = '\0';
            publish_failed(a);
        }
        return;
    }
    if (pb.artwork_url_640[0] == '\0') {
        /* Playing, but this track has no artwork URL. A bare return here
         * (found by review) left the PREVIOUS track's cover on screen under
         * this track's own metadata. Same release+clear+publish-once
         * treatment as "nothing playing" above, keyed off last_track_id so
         * repeat idle polls don't republish every tick. */
        if (strcmp(pb.track_id, a->last_track_id) != 0) {
            if (a->current_handle.generation != 0) {
                artwork_cache_release(&a->cache, a->current_handle);
                a->current_handle = (artwork_handle_t){ 0 };
            }
            strncpy(a->last_track_id, pb.track_id, sizeof(a->last_track_id) - 1);
            publish_failed(a);
        }
        return;
    }
    if (strcmp(pb.track_id, a->last_track_id) == 0) {
        return;   /* already succeeded, or already gave up after
                      ARTWORK_FETCH_MAX_ATTEMPTS -- only re-attempt on a
                      real track-id change */
    }

    char fetching_track_id[PLAYBACK_TRACK_ID_MAX];
    strncpy(fetching_track_id, pb.track_id, sizeof(fetching_track_id) - 1);
    fetching_track_id[sizeof(fetching_track_id) - 1] = '\0';

    /* Real bug, found live: marking a track "done" only on success left
     * last_track_id unchanged on failure, so a cover that repeatedly failed
     * to decode (confirmed: JDR_INP on the same real cover across many
     * independent fetches) got re-fetched every poll for as long as it
     * kept playing, hammering i.scdn.co with an identical, doomed request.
     * attempt_track_id/attempt_count (tracked separately from
     * last_track_id, so an in-progress retry isn't mistaken for "done"
     * above) give a real, bounded chance to recover from transient network
     * flakiness on this long-lived streamed read, without retrying a
     * permanently-bad cover forever. */
    if (strcmp(fetching_track_id, a->attempt_track_id) != 0) {
        strncpy(a->attempt_track_id, fetching_track_id, sizeof(a->attempt_track_id) - 1);
        a->attempt_track_id[sizeof(a->attempt_track_id) - 1] = '\0';
        a->attempt_count = 0;
    }
    a->attempt_count++;
    bool last_attempt = (a->attempt_count >= ARTWORK_FETCH_MAX_ATTEMPTS);

    net_http_request_opts_t opts = { .method = NET_HTTP_METHOD_GET, .retryable_after_send = true };
    net_http_response_t resp = { 0 };
    bool ok = net_http_request(a->http, pb.artwork_url_640, &opts, &a->http_policy,
                                  (char *)a->jpeg_staging, ARTWORK_JPEG_STAGING_BYTES, &resp);
    if (!ok || resp.status_code < 200 || resp.status_code >= 300) {
        ESP_LOGW(TAG, "artwork fetch failed: status=%d attempt=%u/%u", resp.status_code,
                   (unsigned)a->attempt_count, (unsigned)ARTWORK_FETCH_MAX_ATTEMPTS);
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        publish_failed(a);
        return;
    }

    /* Stale-in-flight-fetch discard: re-check before publishing, discard
     * silently if the track changed again during the fetch/decode above --
     * the newer track's own fetch is already queued for a later tick. */
    playback_state_t pb_now;
    if (core_state_read(a->state_store, DOMAIN_PLAYBACK, &pb_now, sizeof(pb_now)) &&
        strcmp(pb_now.track_id, fetching_track_id) != 0) {
        return;
    }

    artwork_handle_t new_handle;
    if (!artwork_cache_acquire(&a->cache, &new_handle)) {
        ESP_LOGW(TAG, "artwork pool exhausted");
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        publish_failed(a);
        return;
    }
    uint16_t *pixels = artwork_cache_lock(&a->cache, new_handle);
    if (pixels == NULL || !artwork_decode_jpeg_to_rgb565(a->jpeg_staging, resp.len, pixels)) {
        /* resp.len logged against the cap: == means genuinely truncated
         * (raise the cap); < means the connection closed early (a network
         * issue the retry above covers, not the buffer size). */
        ESP_LOGW(TAG, "artwork decode failed: resp.len=%u staging_cap=%u attempt=%u/%u",
                   (unsigned)resp.len, (unsigned)ARTWORK_JPEG_STAGING_BYTES,
                   (unsigned)a->attempt_count, (unsigned)ARTWORK_FETCH_MAX_ATTEMPTS);
        artwork_cache_release(&a->cache, new_handle);
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        publish_failed(a);
        return;
    }

    artwork_pixel_buffer_t buf = { pixels, ARTWORK_CACHE_SLOT_WIDTH, ARTWORK_CACHE_SLOT_HEIGHT };
    uint32_t color = artwork_palette_dominant_color_rgb565(&buf);

    artwork_state_t st = { .handle = new_handle, .dominant_color_rgb565 = color };
    strncpy(st.track_id, fetching_track_id, sizeof(st.track_id) - 1);
    core_state_write(a->state_store, DOMAIN_ARTWORK, LINK_STATUS_OK, &st, sizeof(st));

    event_t evt = { .topic = TOPIC_ARTWORK };
    evt.u.artwork.kind                  = ARTWORK_EVT_READY;
    evt.u.artwork.handle               = new_handle;
    evt.u.artwork.dominant_color_rgb565 = color;
    core_eventbus_publish(a->bus, &evt);

    /* Release the PREVIOUS handle strictly AFTER publishing the new one --
     * artwork_cache.h's own ownership contract: there is never a window
     * where core_state/an in-flight event points at an already-released
     * slot. generation == 0 means "no previous handle," core_pool_slot_t's
     * own contract (bumps on acquire, so a real handle is never gen 0). */
    if (a->current_handle.generation != 0) {
        artwork_cache_release(&a->cache, a->current_handle);
    }
    a->current_handle = new_handle;
    mark_track_done(a, fetching_track_id);
}

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
    publish_failed(a);

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
        /* Checked first, before this iteration's own event drain -- a
         * pending suspend should block a NEW fetch from starting, not just
         * eventually take effect after one more. */
        artwork_service_drain_lifecycle_request(a);

        bool any_event = false;
        event_t evt;
        while (core_eventbus_poll(a->bus, a->playback_sub, &evt)) {
            any_event = true;   /* drain fully -- re-read core_state once below, not once per event */
        }
        if (any_event) {
            handle_track_change(a);
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
    publish_failed(a);
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
