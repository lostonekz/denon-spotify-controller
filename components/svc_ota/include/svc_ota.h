/*
 * svc_ota -- the runtime half of OTA. Owns the ota_fsm_t instance and the
 * transient "ota" task's lifecycle; ota_policy.c (Phase 0) still owns every
 * admission decision, ota_verify.c owns the real signature checks, and
 * svc_supervisor owns the health-gate verdict (LIFECYCLE.md §1.4) -- this
 * file is the glue that wires all of them together and drives the FSM.
 *
 * THREADING: ota_fsm_t has no internal lock (core_fsm_t's own design --
 * same reasoning net_wifi_fsm_t's doc comment gives). Three different
 * contexts produce FSM events over this object's lifetime:
 *   1. svc_ota_tick()'s own daily-check timer (OTA_FSM_EVT_CHECK_DUE).
 *   2. The transient "ota" task's outcome, once it finishes a manifest
 *      check or an image download.
 *   3. app_wiring relaying svc_supervisor's health-gate verdict
 *      (OTA_FSM_EVT_GATE_PASS / OTA_FSM_EVT_GATE_TIMED_OUT).
 * (2) never dispatches directly -- it posts to result_queue instead.
 * svc_ota_tick() drains that queue AND owns the CHECK_DUE timer, so it is
 * the ONLY caller of ota_fsm_dispatch() for cases (1) and (2). Case (3)
 * (svc_ota_apply_gate_event()) must be called from that SAME task as
 * svc_ota_tick() -- in practice, both are app_wiring callbacks posted
 * through the same svc_scheduler worker task, exactly like every other
 * Phase 1/2 "tick" consumer, so this falls out for free rather than
 * needing its own lock.
 *
 * svc_ota has no svc_supervisor edge (svc has no self-edge,
 * arch_rules.json) -- app_wiring is the one that polls
 * svc_supervisor_health_gate_verdict() and translates HEALTH_GATE_PASS/
 * HEALTH_GATE_TIMED_OUT into the matching OTA_FSM_EVT_GATE_* call here,
 * which is why svc_ota_apply_gate_event() takes an ota_fsm_event_t (a type
 * this component already owns) rather than svc_supervisor's own
 * health_gate_verdict_t.
 */
#ifndef SVC_OTA_H
#define SVC_OTA_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_partition.h"

#include "core_eventbus.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "net_http.h"
#include "ota_fsm.h"
#include "ota_policy.h"
#include "ota_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_OTA_RESULT_QUEUE_DEPTH 4

typedef struct {
    ota_fsm_t              fsm;

    core_eventbus_t          *bus;
    net_http_t                 *http;
    kv_store_if_t                kv;             /* "ota" namespace, opened by app_wiring */

    http_policy_t                 http_policy;
    const char                    *manifest_url;
    bool                            force_check_on_boot;

    ota_verify_ctx_t                verify_ctx;
    ota_verifier_t                    verifier;

    ota_device_state_t                 dev_state;   /* running_build = compiled-in;
                                                          running_security_version =
                                                          max(compiled-in, persisted "sec_ver_max") */
    uint32_t                             heap_watermark_baseline;   /* persisted "heap_wm"; 0 until first mark_valid */

    QueueHandle_t                          result_queue;
    int64_t                                  next_check_at_us;

    const esp_partition_t                     *pending_boot_partition;   /* set by the download task
                                                                              before posting IMAGE_ADMITTED */

    /* PLATFORM.md §2.2's "one successful TLS handshake to any host" gate
     * condition -- true from the first time the download task gets any
     * genuine HTTP response (net_http_get()/net_http_stream_open()
     * reporting ok, regardless of status code or the eventual policy
     * verdict): that requires a completed mbedTLS handshake against the
     * OTA host, which is exactly what the gate wants proof of. Set by the
     * transient task, polled by app_wiring via svc_ota_tls_handshake_proven()
     * -- an ordinary bool, not queued through result_queue, since it is a
     * sticky fact ("has this ever happened"), not an FSM transition. */
    bool                                        tls_handshake_proven;
} svc_ota_t;

typedef struct {
    core_eventbus_t              *bus;
    net_http_t                     *http;
    kv_store_if_t                     kv;

    http_policy_t                       http_policy;
    const char                          *manifest_url;
    bool                                  force_check_on_boot;

    const ota_trust_anchor_t              *anchors;        /* e.g. ota_trust_anchor_dev.c's table (Tier 5) */
    size_t                                   anchor_count;

    uint32_t                                  compiled_build;             /* this build's own build number */
    uint32_t                                  compiled_security_version;  /* this build's own security_version */
} svc_ota_cfg_t;

/* Boot-time flow (LIFECYCLE.md §1.4, a first-class step, not a side
 * effect): checks esp_ota_get_state_partition() on the running partition
 * and selects ota_fsm_init()'s initial_state accordingly --
 * OTA_FSM_STATE_PENDING_VERIFY if this boot is a fresh, unconfirmed OTA
 * image; OTA_FSM_STATE_IDLE otherwise. Loads the persisted "sec_ver_max"/
 * "heap_wm" NVS values (0 if absent -- first boot ever) and folds
 * sec_ver_max into dev_state.running_security_version per ota_policy.h's
 * "highest ever run" contract. */
bool svc_ota_create(svc_ota_t *o, const svc_ota_cfg_t *cfg);

/* Call periodically (recommended: every ~1 s) via a svc_scheduler periodic
 * job posted by app_wiring -- see this header's own THREADING note above
 * for why this is the only place OTA_FSM_EVT_CHECK_DUE and the download
 * task's outcome events get dispatched. */
void svc_ota_tick(svc_ota_t *o);

/* event must be OTA_FSM_EVT_GATE_PASS or OTA_FSM_EVT_GATE_TIMED_OUT --
 * anything else is a caller bug and a no-op. Must run on the same task as
 * svc_ota_tick(), see this header's THREADING note. */
void svc_ota_apply_gate_event(svc_ota_t *o, ota_fsm_event_t event);

ota_fsm_state_t svc_ota_current_state(const svc_ota_t *o);

/* See svc_ota_t.tls_handshake_proven's own comment above. */
bool svc_ota_tls_handshake_proven(const svc_ota_t *o);

/* The persisted "heap_wm" baseline (0 if this device has never completed a
 * mark_valid) -- app_wiring compares esp_get_free_heap_size() against this
 * for HEALTH_COND_HEAP_WATERMARK ("free heap >= 85% of baseline",
 * PLATFORM.md §2.2). svc_ota has no opinion on what "ok" means here, only
 * on what the baseline currently is. */
uint32_t svc_ota_heap_watermark_baseline(const svc_ota_t *o);

/* Not in LIFECYCLE.md §1.2's restart table -- defaults to the conservative
 * choice, same reasoning as net_wifi/net_time/net_http. degraded_ok = true:
 * no OTA just means the device can't self-update, not fatal. */
static const service_desc_t SVC_OTA_SERVICE_DESC = {
    .id           = SVC_ID_OTA,
    .name         = "svc_ota",
    .requires     = SVC_BIT(SVC_ID_STORAGE),
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = true,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* SVC_OTA_H */
