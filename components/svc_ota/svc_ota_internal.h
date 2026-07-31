/*
 * Private declarations shared between svc_ota.c, svc_ota_download.c, and
 * svc_ota_apply.c -- nothing outside this component (not even app_wiring)
 * needs any of this, so it stays out of include/.
 */
#ifndef SVC_OTA_INTERNAL_H
#define SVC_OTA_INTERNAL_H

#include "ota_fsm.h"
#include "svc_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The "ota" NVS namespace's two keys (svc_ota.h's top comment). Shared so
 * svc_ota_apply.c's mark_valid_cancel_rollback action can persist the same
 * keys svc_ota.c reads at construction. */
#define OTA_KV_KEY_SEC_VER_MAX "sec_ver_max"
#define OTA_KV_KEY_HEAP_WM     "heap_wm"

uint32_t svc_ota_kv_get_u32(kv_store_if_t *kv, const char *key, uint32_t default_value);
void svc_ota_kv_set_u32(kv_store_if_t *kv, const char *key, uint32_t v);

/* Posted by the transient "ota" task (svc_ota_download.c) into
 * svc_ota_t.result_queue; drained only by svc_ota_tick() -- see svc_ota.h's
 * top comment for why every ota_fsm_t dispatch must funnel through that one
 * call path. */
typedef struct {
    ota_fsm_event_t event;
    ota_admit_t       verdict;   /* OTA_ADMIT_OK on the two ADMITTED events; the
                                    rejection reason on REJECTED; unused,
                                    always OTA_REJECT_MALFORMED_MANIFEST, on
                                    DOWNLOAD_FAILED (no policy verdict to have) */
} ota_task_result_msg_t;

/* Spawns the transient "ota" task (ARCHITECTURE.md §4: core 0, prio 3,
 * 8 KB, created on demand, destroyed after). The task owns the whole
 * manifest-check-then-image-download sequence and posts exactly one
 * ota_task_result_msg_t before it exits. */
void svc_ota_download_task_start(svc_ota_t *o);

/* ota_fsm_actions_t implementations -- svc_ota.c (construction/tick logic,
 * plus the two "just log and report" actions) and svc_ota_apply.c (the
 * three actions that end in esp_ota_set_boot_partition()/
 * esp_ota_mark_app_valid_cancel_rollback()/
 * esp_ota_mark_app_invalid_rollback_and_reboot(), grouped together since
 * they share that "apply the verdict, possibly reboot" character) split
 * ownership of the vtable in ota_fsm_actions_t; both need each other's
 * action_fn addresses to build the one table svc_ota_create() wires up. */
void svc_ota_action_start_manifest_check(void *ctx);
void svc_ota_action_start_download(void *ctx);
void svc_ota_action_report_check_failed(void *ctx);
void svc_ota_action_report_download_failed(void *ctx);

void svc_ota_action_mark_valid_cancel_rollback(void *ctx);
void svc_ota_action_mark_invalid_rollback_and_reboot(void *ctx);
void svc_ota_action_set_boot_partition_and_reboot(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SVC_OTA_INTERNAL_H */
