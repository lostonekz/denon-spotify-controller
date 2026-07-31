#include "svc_ota.h"

#include "svc_ota_internal.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char *TAG = "svc_ota";

/* "Daily at a randomised hour" (PLATFORM.md §2.4) without needing net_time's
 * wall-clock/timezone awareness: the FIRST check's time-of-day is
 * randomised (a uniform offset into the next 24h), which already avoids a
 * thundering herd across many devices since each one's boot time and
 * random offset differ; every check after that recurs on a fixed 24h
 * cadence from that anchor. Documented judgement call, not pinned by any
 * doc. */
#define OTA_CHECK_INTERVAL_US ((int64_t)24 * 60 * 60 * 1000000)

/* EVENTS_AND_STATE.md §6.4: "every transition is logged... this log is the
 * entire debugging story" -- exactly what a scripted UART capture watches
 * for during Tier 7's rollback/wrong-key/downgrade proofs. */
static const char *ota_fsm_state_name(uint32_t s)
{
    switch ((ota_fsm_state_t)s) {
    case OTA_FSM_STATE_PENDING_VERIFY: return "PENDING_VERIFY";
    case OTA_FSM_STATE_IDLE:           return "IDLE";
    case OTA_FSM_STATE_CHECKING:       return "CHECKING";
    case OTA_FSM_STATE_DOWNLOADING:    return "DOWNLOADING";
    case OTA_FSM_STATE_REBOOT_PENDING: return "REBOOT_PENDING";
    case OTA_FSM_STATE_ROLLING_BACK:   return "ROLLING_BACK";
    default:                          return "?";
    }
}

static const char *ota_fsm_event_name(uint32_t e)
{
    switch ((ota_fsm_event_t)e) {
    case OTA_FSM_EVT_GATE_PASS:          return "GATE_PASS";
    case OTA_FSM_EVT_GATE_TIMED_OUT:     return "GATE_TIMED_OUT";
    case OTA_FSM_EVT_CHECK_DUE:          return "CHECK_DUE";
    case OTA_FSM_EVT_MANIFEST_ADMITTED:  return "MANIFEST_ADMITTED";
    case OTA_FSM_EVT_MANIFEST_REJECTED:  return "MANIFEST_REJECTED";
    case OTA_FSM_EVT_IMAGE_ADMITTED:     return "IMAGE_ADMITTED";
    case OTA_FSM_EVT_IMAGE_REJECTED:     return "IMAGE_REJECTED";
    case OTA_FSM_EVT_DOWNLOAD_FAILED:    return "DOWNLOAD_FAILED";
    default:                            return "?";
    }
}

static void ota_fsm_transition_logger(void *log_ctx, const char *fsm_name,
                                        uint32_t from_state, uint32_t event, uint32_t to_state)
{
    (void)log_ctx;
    ESP_LOGI(TAG, "[fsm:%s] %s --(%s)--> %s", fsm_name,
              ota_fsm_state_name(from_state), ota_fsm_event_name(event), ota_fsm_state_name(to_state));
}

uint32_t svc_ota_kv_get_u32(kv_store_if_t *kv, const char *key, uint32_t default_value)
{
    uint32_t v;
    size_t len = sizeof(v);
    if (kv->get_blob(kv->ctx, key, &v, &len) && len == sizeof(v)) {
        return v;
    }
    return default_value;
}

bool svc_ota_kv_set_u32(kv_store_if_t *kv, const char *key, uint32_t v)
{
    /* Tier -1 fix: both return values were previously discarded -- for the
     * anti-rollback floor (OTA_KV_KEY_SEC_VER_MAX), a silently-failed
     * persist here is exactly the failure mode R15 exists to prevent. See
     * svc_ota_apply.c's caller. */
    bool ok = kv->set_blob(kv->ctx, key, &v, sizeof(v));
    ok = kv->commit(kv->ctx) && ok;
    return ok;
}

bool svc_ota_create(svc_ota_t *o, const svc_ota_cfg_t *cfg)
{
    memset(o, 0, sizeof(*o));

    o->bus                 = cfg->bus;
    o->http                = cfg->http;
    o->kv                  = cfg->kv;
    o->http_policy          = cfg->http_policy;
    o->manifest_url         = cfg->manifest_url;
    o->force_check_on_boot = cfg->force_check_on_boot;

    o->verify_ctx.trust.anchors = cfg->anchors;
    o->verify_ctx.trust.count   = cfg->anchor_count;
    o->verifier.is_trusted_key      = ota_verify_is_trusted_key;
    o->verifier.verify_manifest_sig = ota_verify_manifest_sig;
    o->verifier.verify_image_sig    = ota_verify_image_sig;
    o->verifier.ctx                 = &o->verify_ctx;

    uint32_t persisted_sec_ver_max = svc_ota_kv_get_u32(&o->kv, OTA_KV_KEY_SEC_VER_MAX, 0);
    o->dev_state.running_build            = cfg->compiled_build;
    o->dev_state.running_security_version = (persisted_sec_ver_max > cfg->compiled_security_version)
                                                 ? persisted_sec_ver_max
                                                 : cfg->compiled_security_version;

    o->heap_watermark_baseline = svc_ota_kv_get_u32(&o->kv, OTA_KV_KEY_HEAP_WM, 0);

    o->result_queue = xQueueCreate(SVC_OTA_RESULT_QUEUE_DEPTH, sizeof(ota_task_result_msg_t));
    if (o->result_queue == NULL) {
        ESP_LOGE(TAG, "failed to create result queue");
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_VALID;
    bool pending_verify = (running != NULL) &&
                          (esp_ota_get_state_partition(running, &state) == ESP_OK) &&
                          (state == ESP_OTA_IMG_PENDING_VERIFY);

    ota_fsm_actions_t actions = {
        .mark_valid_cancel_rollback       = svc_ota_action_mark_valid_cancel_rollback,
        .mark_invalid_rollback_and_reboot = svc_ota_action_mark_invalid_rollback_and_reboot,
        .start_manifest_check             = svc_ota_action_start_manifest_check,
        .start_download                   = svc_ota_action_start_download,
        .report_check_failed              = svc_ota_action_report_check_failed,
        .set_boot_partition_and_reboot     = svc_ota_action_set_boot_partition_and_reboot,
        .report_download_failed           = svc_ota_action_report_download_failed,
        .ctx                              = o,
    };
    ota_fsm_init(&o->fsm, pending_verify ? OTA_FSM_STATE_PENDING_VERIFY : OTA_FSM_STATE_IDLE,
                 &actions, ota_fsm_transition_logger, NULL);

    int64_t now = esp_timer_get_time();
    int64_t initial_offset_us = (int64_t)(esp_random() % (uint32_t)(OTA_CHECK_INTERVAL_US / 1000)) * 1000;
    o->next_check_at_us = cfg->force_check_on_boot ? now : (now + initial_offset_us);

    ESP_LOGI(TAG, "created: initial_state=%d running_build=%" PRIu32 " running_security_version=%" PRIu32,
             (int)ota_fsm_current_state(&o->fsm), o->dev_state.running_build, o->dev_state.running_security_version);
    return true;
}

void svc_ota_tick(svc_ota_t *o)
{
    ota_task_result_msg_t msg;
    while (xQueueReceive(o->result_queue, &msg, 0) == pdTRUE) {
        ota_fsm_dispatch(&o->fsm, msg.event, &msg.verdict);
    }

    if (ota_fsm_current_state(&o->fsm) == OTA_FSM_STATE_IDLE) {
        int64_t now = esp_timer_get_time();
        if (now >= o->next_check_at_us) {
            o->next_check_at_us = now + OTA_CHECK_INTERVAL_US;
            ota_fsm_dispatch(&o->fsm, OTA_FSM_EVT_CHECK_DUE, NULL);
        }
    }
}

void svc_ota_apply_gate_event(svc_ota_t *o, ota_fsm_event_t event)
{
    if (event != OTA_FSM_EVT_GATE_PASS && event != OTA_FSM_EVT_GATE_TIMED_OUT) {
        return;
    }
    ota_fsm_dispatch(&o->fsm, event, NULL);
}

ota_fsm_state_t svc_ota_current_state(const svc_ota_t *o)
{
    return ota_fsm_current_state(&o->fsm);
}

bool svc_ota_tls_handshake_proven(const svc_ota_t *o)
{
    return o->tls_handshake_proven;
}

uint32_t svc_ota_heap_watermark_baseline(const svc_ota_t *o)
{
    return o->heap_watermark_baseline;
}

void svc_ota_action_start_manifest_check(void *ctx)
{
    svc_ota_t *o = (svc_ota_t *)ctx;
    ESP_LOGI(TAG, "manifest check starting");
    svc_ota_download_task_start(o);
}

void svc_ota_action_start_download(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "manifest admitted, downloading image");
    /* No-op beyond the log line: the download task (already running --
     * it posted OTA_FSM_EVT_MANIFEST_ADMITTED to get here) continues into
     * the download phase in its own control flow. See svc_ota_download.c. */
}

void svc_ota_action_report_check_failed(void *ctx)
{
    svc_ota_t *o = (svc_ota_t *)ctx;
    ESP_LOGW(TAG, "manifest check failed/rejected");

    event_t evt = { .topic = TOPIC_OTA };
    evt.u.ota.kind = OTA_EVT_FAILED;
    core_eventbus_publish(o->bus, &evt);
}

void svc_ota_action_report_download_failed(void *ctx)
{
    svc_ota_t *o = (svc_ota_t *)ctx;
    ESP_LOGW(TAG, "image download failed/rejected");

    event_t evt = { .topic = TOPIC_OTA };
    evt.u.ota.kind = OTA_EVT_FAILED;
    core_eventbus_publish(o->bus, &evt);
}
