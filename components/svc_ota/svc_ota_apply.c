/*
 * The three ota_fsm_actions_t callbacks that apply a verdict by touching
 * the boot partition or the rollback flag, grouped here since all three
 * end in either "confirmed, persist the new baseline" or "reboot" --
 * svc_ota.c's own actions never do either.
 */
#include "svc_ota_internal.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "svc_ota_apply";

void svc_ota_action_mark_valid_cancel_rollback(void *ctx)
{
    svc_ota_t *o = (svc_ota_t *)ctx;

    /* PLATFORM.md §2.2's gate passing is exactly the moment this boot's
     * security_version becomes a trustworthy new baseline -- persist it
     * BEFORE calling esp_ota_mark_app_valid_cancel_rollback(), not after
     * (Tier -1 fix: an earlier version of this function cancelled rollback
     * first unconditionally, which would have made a subsequent
     * mark-invalid-and-reboot call on a persist failure run against an
     * image already marked valid -- too late to matter). A silently-failed
     * persist of the anti-rollback floor is exactly the failure mode R15
     * exists to prevent: accepting this image as valid while its
     * security_version bump never durably landed would mean a future
     * rollback check could still compare against the OLD, lower floor. If
     * the persist fails, log loudly and roll back immediately instead of
     * ever confirming: the previous image is known-good, and re-attempting
     * the same update is safe, whereas silently confirming an unrecorded
     * floor bump is not. */
    if (o->dev_state.running_security_version > svc_ota_kv_get_u32(&o->kv, OTA_KV_KEY_SEC_VER_MAX, 0)) {
        if (!svc_ota_kv_set_u32(&o->kv, OTA_KV_KEY_SEC_VER_MAX, o->dev_state.running_security_version)) {
            ESP_LOGE(TAG, "failed to persist anti-rollback floor (security_version=%" PRIu32
                     ") -- rolling back rather than confirming an unrecorded floor bump",
                     o->dev_state.running_security_version);
            esp_ota_mark_app_invalid_rollback_and_reboot();   /* does not return */
            return;
        }
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %d", (int)err);
    }

    /* Heap watermark baseline: diagnostic only (R6), not security-critical --
     * log and carry on rather than rolling back a good image over it. */
    o->heap_watermark_baseline = esp_get_minimum_free_heap_size();
    if (!svc_ota_kv_set_u32(&o->kv, OTA_KV_KEY_HEAP_WM, o->heap_watermark_baseline)) {
        ESP_LOGE(TAG, "failed to persist heap watermark baseline (non-fatal)");
    }

    ESP_LOGI(TAG, "health gate passed: image confirmed valid");
}

void svc_ota_action_mark_invalid_rollback_and_reboot(void *ctx)
{
    (void)ctx;
    ESP_LOGE(TAG, "health gate timed out: rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();   /* does not return */
}

void svc_ota_action_set_boot_partition_and_reboot(void *ctx)
{
    svc_ota_t *o = (svc_ota_t *)ctx;

    if (o->pending_boot_partition == NULL) {
        ESP_LOGE(TAG, "set_boot_partition_and_reboot fired with no pending partition");
        return;
    }

    esp_err_t err = esp_ota_set_boot_partition(o->pending_boot_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", (int)err);
        return;
    }

    ESP_LOGI(TAG, "new image admitted, rebooting to apply");
    esp_restart();   /* does not return */
}
