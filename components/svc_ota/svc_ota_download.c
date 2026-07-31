/*
 * The transient "ota" task (docs/ARCHITECTURE.md §4: core 0, prio 3, 8 KB,
 * created on demand, destroyed after). Owns the whole manifest-check-then-
 * image-download sequence and posts exactly one ota_task_result_msg_t to
 * svc_ota_t.result_queue before it exits -- it never dispatches into
 * ota_fsm_t directly (svc_ota.h's THREADING note explains why).
 *
 * Deliberately does not use esp_https_ota: esp_https_ota_finish() sets the
 * boot partition internally, with no window to run ota_policy_admit_image()
 * first. Streams via net_http_stream_*() instead, feeding esp_ota_write()
 * and a running mbedtls_sha256 in the same loop, then calls esp_ota_end()
 * (validates the image; does not touch the boot partition) before ever
 * calling ota_policy_admit_image() -- esp_ota_set_boot_partition() only
 * happens later, in svc_ota_apply.c's action, and only if that call
 * returns OTA_ADMIT_OK.
 */
#include "svc_ota_internal.h"

#include <inttypes.h>

#include "freertos/task.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"

#include "ota_manifest.h"

static const char *TAG = "svc_ota_download";

#define OTA_TASK_STACK 8192
#define OTA_TASK_PRIO  3
#define OTA_TASK_CORE  0

#define OTA_MANIFEST_BUF_LEN   1024
#define OTA_DOWNLOAD_CHUNK_LEN 1024

static void post_result(svc_ota_t *o, ota_fsm_event_t event, ota_admit_t verdict)
{
    ota_task_result_msg_t msg = { .event = event, .verdict = verdict };
    /* Depth-4 queue, one message per task lifetime -- this must never
     * block the transient task on a full queue silently swallowing its
     * only outcome. */
    if (xQueueSend(o->result_queue, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "result queue full -- outcome dropped (event=%d)", (int)event);
    }
}

static ota_admit_t check_manifest(svc_ota_t *o, ota_manifest_full_t *full, ota_manifest_t *projected)
{
    char buf[OTA_MANIFEST_BUF_LEN];
    net_http_response_t resp = { 0 };

    /* net_http_get() acquires and releases its own TLS slot internally
     * (net_http.c) -- an outer acquire/release here was redundant and burned
     * both of the arbiter's 2 total slots for a 1KB manifest fetch (Tier -1
     * fix: harmless when svc_ota was the arbiter's only caller, but directly
     * contended once Spotify/artwork share it). */
    bool ok = net_http_get(o->http, o->manifest_url, &o->http_policy, buf, sizeof(buf), &resp);

    if (ok) {
        o->tls_handshake_proven = true;   /* a real response, over TLS -- see svc_ota.h's comment */
    }
    if (!ok || resp.status_code != 200) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }

    ota_admit_t parse_verdict = ota_manifest_parse_json(buf, resp.len, full);
    if (parse_verdict != OTA_ADMIT_OK) {
        return parse_verdict;
    }

    ota_manifest_project(full, projected);

    o->verify_ctx.pending_full_manifest = full;
    ota_admit_t verdict = ota_policy_admit_manifest(projected, &o->dev_state, &o->verifier);
    o->verify_ctx.pending_full_manifest = NULL;
    return verdict;
}

typedef struct {
    bool                       transport_failed;   /* true: verdict below is meaningless */
    ota_admit_t                verdict;
    const esp_partition_t     *target;
    uint32_t                    total_bytes;
} download_result_t;

static download_result_t download_image(svc_ota_t *o, const ota_manifest_full_t *full,
                                          const ota_manifest_t *projected)
{
    download_result_t r = { .transport_failed = true, .verdict = OTA_ADMIT_OK, .target = NULL, .total_bytes = 0 };

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        ESP_LOGE(TAG, "no OTA update partition available");
        return r;
    }

    if (!net_http_tls_acquire(o->http, 2, o->http_policy.connect_timeout_ms)) {
        return r;
    }

    esp_ota_handle_t handle;
    if (esp_ota_begin(target, projected->image_size, &handle) != ESP_OK) {
        net_http_tls_release(o->http, 2);
        return r;
    }

    net_http_stream_t stream;
    int status = 0;
    uint32_t retry_after_s = 0;
    if (!net_http_stream_open(full->url, &o->http_policy, &stream, &status, &retry_after_s)) {
        esp_ota_abort(handle);
        net_http_tls_release(o->http, 2);
        return r;
    }
    o->tls_handshake_proven = true;   /* headers received, over TLS -- see svc_ota.h's comment */
    if (status != 200) {
        net_http_stream_close(&stream);
        esp_ota_abort(handle);
        net_http_tls_release(o->http, 2);
        return r;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    uint8_t chunk[OTA_DOWNLOAD_CHUNK_LEN];
    uint32_t total = 0;
    uint8_t last_pct = 0;
    bool io_error = false;

    for (;;) {
        int n = net_http_stream_read(&stream, chunk, sizeof(chunk));
        if (n < 0) {
            io_error = true;
            break;
        }
        if (n == 0) {
            break;   /* EOF */
        }
        /* Tier -1 fix: bound BEFORE writing/hashing this chunk, not after --
         * checking post-write still lets a corrupted/mismatched image write
         * up to one full chunk past the declared size. */
        if (ota_download_would_overflow(total, n, projected->image_size)) {
            io_error = true;
            break;
        }
        if (esp_ota_write(handle, chunk, (size_t)n) != ESP_OK) {
            io_error = true;
            break;
        }
        mbedtls_sha256_update(&sha_ctx, chunk, (size_t)n);
        total += (uint32_t)n;

        if (projected->image_size > 0) {
            uint8_t pct = (uint8_t)((uint64_t)total * 100u / projected->image_size);
            if (pct != last_pct) {
                last_pct = pct;
                event_t evt = { .topic = TOPIC_OTA };
                evt.u.ota.kind          = OTA_EVT_PROGRESS;
                evt.u.ota.progress_pct = pct;
                core_eventbus_publish(o->bus, &evt);
            }
        }
    }
    net_http_stream_close(&stream);

    uint8_t actual_sha256[32];
    mbedtls_sha256_finish(&sha_ctx, actual_sha256);
    mbedtls_sha256_free(&sha_ctx);

    if (io_error) {
        esp_ota_abort(handle);
        net_http_tls_release(o->http, 2);
        return r;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        /* esp_ota_end() already checked magic/descriptor/RSA-3072 image
         * signature -- a failure here IS a real verdict, not a transport
         * issue. */
        net_http_tls_release(o->http, 2);
        r.transport_failed = false;
        r.verdict            = OTA_REJECT_BAD_IMAGE_SIG;
        return r;
    }

    o->verify_ctx.target_partition = target;
    ota_admit_t verdict = ota_policy_admit_image(projected, total, actual_sha256, &o->verifier);
    o->verify_ctx.target_partition = NULL;

    net_http_tls_release(o->http, 2);

    r.transport_failed = false;
    r.verdict            = verdict;
    r.target             = target;
    r.total_bytes        = total;
    return r;
}

static void ota_download_task(void *pv)
{
    svc_ota_t *o = (svc_ota_t *)pv;

    ota_manifest_full_t full;
    ota_manifest_t projected;
    ota_admit_t manifest_verdict = check_manifest(o, &full, &projected);

    if (manifest_verdict != OTA_ADMIT_OK) {
        ESP_LOGW(TAG, "manifest not admitted: %s", ota_admit_str(manifest_verdict));
        post_result(o, OTA_FSM_EVT_MANIFEST_REJECTED, manifest_verdict);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "manifest admitted (build %" PRIu32 "), downloading image", projected.build);
    post_result(o, OTA_FSM_EVT_MANIFEST_ADMITTED, OTA_ADMIT_OK);

    download_result_t dr = download_image(o, &full, &projected);

    if (dr.transport_failed) {
        ESP_LOGW(TAG, "image download transport failure");
        post_result(o, OTA_FSM_EVT_DOWNLOAD_FAILED, OTA_REJECT_MALFORMED_MANIFEST);
    } else if (dr.verdict != OTA_ADMIT_OK) {
        ESP_LOGW(TAG, "image not admitted: %s", ota_admit_str(dr.verdict));
        post_result(o, OTA_FSM_EVT_IMAGE_REJECTED, dr.verdict);
    } else {
        o->pending_boot_partition = dr.target;
        ESP_LOGI(TAG, "image admitted (%" PRIu32 " bytes), ready to reboot", dr.total_bytes);
        post_result(o, OTA_FSM_EVT_IMAGE_ADMITTED, OTA_ADMIT_OK);
    }

    vTaskDelete(NULL);
}

void svc_ota_download_task_start(svc_ota_t *o)
{
    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(ota_download_task, "ota", OTA_TASK_STACK,
                                             o, OTA_TASK_PRIO, &handle, OTA_TASK_CORE);
    if (ok != pdPASS || handle == NULL) {
        ESP_LOGE(TAG, "failed to create ota task");
        post_result(o, OTA_FSM_EVT_MANIFEST_REJECTED, OTA_REJECT_MALFORMED_MANIFEST);
    }
}
