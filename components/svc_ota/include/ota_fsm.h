/*
 * The runtime OTA state machine. docs/EVENTS_AND_STATE.md §6.4 explicitly
 * lists `ota` among the FSMs that must be table-driven on core_fsm_t.
 *
 * Built the same way net_wifi_fsm.c was: side effects injected via
 * ota_fsm_actions_t (svc_ota.c supplies the real esp_ota_ops.h/net_http
 * calls; host tests supply call-counting fakes), transition logging
 * injected the same way core_fsm_t already requires.
 *
 * PENDING_VERIFY is not just another state -- it is what makes "never start
 * a new download while a previous update is unconfirmed" true by
 * construction. A fresh boot into ESP_OTA_IMG_PENDING_VERIFY (checked once,
 * at construction, by svc_ota_create() -- a first-class flow per
 * docs/LIFECYCLE.md §1.4, not a side effect) selects PENDING_VERIFY as
 * ota_fsm_init()'s initial_state instead of the usual IDLE. There is
 * deliberately no PENDING_VERIFY row in the transition table for
 * OTA_FSM_EVT_CHECK_DUE (or anything else) -- core_fsm_dispatch() returning
 * FSM_NO_TRANSITION for every event but the two gate-resolution ones is
 * what blocks every other state's entry, not a convention svc_ota.c has to
 * remember to honour.
 *
 * Named ota_fsm_state_t/ota_fsm_event_t (not ota_state_t/ota_event_t) to
 * stay clearly distinct from event_types.h's ota_evt_kind_t and its
 * OTA_EVT_ constants (the public TOPIC_OTA payload, a different enum for a
 * different audience -- UI/diagnostics, not this FSM's own internal
 * transitions) and from ota_policy.h's ota_admit_t and its OTA_ADMIT_ and
 * OTA_REJECT_ constants (the admission verdict, not a state).
 *
 * Phase 2 scope note: PLATFORM.md section 2.4's "apply automatically only
 * when idle" needs svc_denon/svc_spotify/hal_display state that doesn't
 * exist until Phase 3+. DOWNLOADING proceeds as soon as CHECKING admits a
 * manifest -- there is no idle-wait state yet. Documented gap, not a
 * silent cut, same treatment as the health gate's panel/UI stand-in.
 */
#ifndef OTA_FSM_H
#define OTA_FSM_H

#include <stdint.h>

#include "core_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_FSM_STATE_PENDING_VERIFY = 0,   /* boot-time only initial_state; see above */
    OTA_FSM_STATE_IDLE,
    OTA_FSM_STATE_CHECKING,
    OTA_FSM_STATE_DOWNLOADING,
    OTA_FSM_STATE_REBOOT_PENDING,       /* image admitted, boot partition set, about to restart */
    OTA_FSM_STATE_ROLLING_BACK,         /* gate timed out, about to restart into the old slot */
} ota_fsm_state_t;

typedef enum {
    OTA_FSM_EVT_GATE_PASS = 0,          /* PENDING_VERIFY only */
    OTA_FSM_EVT_GATE_TIMED_OUT,         /* PENDING_VERIFY only */
    OTA_FSM_EVT_CHECK_DUE,              /* IDLE -- daily cadence or on-demand */
    OTA_FSM_EVT_MANIFEST_ADMITTED,      /* CHECKING -- ota_policy_admit_manifest() == OTA_ADMIT_OK */
    OTA_FSM_EVT_MANIFEST_REJECTED,      /* CHECKING -- any other ota_admit_t */
    OTA_FSM_EVT_IMAGE_ADMITTED,         /* DOWNLOADING -- ota_policy_admit_image() == OTA_ADMIT_OK */
    OTA_FSM_EVT_IMAGE_REJECTED,         /* DOWNLOADING -- any other ota_admit_t */
    OTA_FSM_EVT_DOWNLOAD_FAILED,        /* DOWNLOADING -- transport failure, no verdict to have */
} ota_fsm_event_t;

typedef struct {
    /* PENDING_VERIFY resolution -- LIFECYCLE.md §1.4's verdict, relayed by
     * app_wiring from svc_supervisor. Neither call returns on target
     * (esp_ota_mark_app_valid_cancel_rollback() just clears the pending
     * flag; esp_ota_mark_app_invalid_rollback_and_reboot() reboots), so the
     * committed next_state is never actually observed running -- it exists
     * so the table stays total and host tests can assert the right call
     * fired without a real reboot. */
    void (*mark_valid_cancel_rollback)(void *ctx);
    void (*mark_invalid_rollback_and_reboot)(void *ctx);

    void (*start_manifest_check)(void *ctx);
    void (*start_download)(void *ctx);
    void (*report_check_failed)(void *ctx);
    void (*set_boot_partition_and_reboot)(void *ctx);   /* also never returns on target */
    void (*report_download_failed)(void *ctx);

    void *ctx;
} ota_fsm_actions_t;

typedef struct {
    core_fsm_t          fsm;
    ota_fsm_actions_t     actions;
} ota_fsm_t;

/* initial_state is caller-chosen (svc_ota_create()'s own boot-time
 * esp_ota_get_state_partition() check), not fixed at OTA_FSM_STATE_IDLE
 * like net_wifi_fsm_init()'s BOOT -- that check is exactly what decides
 * between OTA_FSM_STATE_PENDING_VERIFY and OTA_FSM_STATE_IDLE. */
void ota_fsm_init(ota_fsm_t *ofsm, ota_fsm_state_t initial_state,
                    const ota_fsm_actions_t *actions,
                    fsm_transition_log_fn on_transition, void *log_ctx);

fsm_result_t ota_fsm_dispatch(ota_fsm_t *ofsm, ota_fsm_event_t event, const void *event_data);

ota_fsm_state_t ota_fsm_current_state(const ota_fsm_t *ofsm);

#ifdef __cplusplus
}
#endif

#endif /* OTA_FSM_H */
