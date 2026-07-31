#include "ota_fsm.h"

#include <stddef.h>

static ota_fsm_actions_t *actions_of(void *ctx)
{
    ota_fsm_t *ofsm = (ota_fsm_t *)ctx;
    return &ofsm->actions;
}

static void action_mark_valid(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->mark_valid_cancel_rollback != NULL) a->mark_valid_cancel_rollback(a->ctx);
}

static void action_mark_invalid_reboot(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->mark_invalid_rollback_and_reboot != NULL) a->mark_invalid_rollback_and_reboot(a->ctx);
}

static void action_start_manifest_check(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->start_manifest_check != NULL) a->start_manifest_check(a->ctx);
}

static void action_start_download(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->start_download != NULL) a->start_download(a->ctx);
}

static void action_report_check_failed(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->report_check_failed != NULL) a->report_check_failed(a->ctx);
}

static void action_set_boot_partition_and_reboot(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->set_boot_partition_and_reboot != NULL) a->set_boot_partition_and_reboot(a->ctx);
}

static void action_report_download_failed(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    ota_fsm_actions_t *a = actions_of(ctx);
    if (a->report_download_failed != NULL) a->report_download_failed(a->ctx);
}

static const fsm_transition_t OTA_FSM_TRANSITIONS[] = {
    { OTA_FSM_STATE_PENDING_VERIFY, OTA_FSM_EVT_GATE_PASS,          action_mark_valid,                    OTA_FSM_STATE_IDLE },
    { OTA_FSM_STATE_PENDING_VERIFY, OTA_FSM_EVT_GATE_TIMED_OUT,     action_mark_invalid_reboot,            OTA_FSM_STATE_ROLLING_BACK },
    { OTA_FSM_STATE_IDLE,           OTA_FSM_EVT_CHECK_DUE,          action_start_manifest_check,          OTA_FSM_STATE_CHECKING },
    { OTA_FSM_STATE_CHECKING,       OTA_FSM_EVT_MANIFEST_ADMITTED,  action_start_download,                OTA_FSM_STATE_DOWNLOADING },
    { OTA_FSM_STATE_CHECKING,       OTA_FSM_EVT_MANIFEST_REJECTED,  action_report_check_failed,           OTA_FSM_STATE_IDLE },
    { OTA_FSM_STATE_DOWNLOADING,    OTA_FSM_EVT_IMAGE_ADMITTED,     action_set_boot_partition_and_reboot, OTA_FSM_STATE_REBOOT_PENDING },
    { OTA_FSM_STATE_DOWNLOADING,    OTA_FSM_EVT_IMAGE_REJECTED,     action_report_download_failed,        OTA_FSM_STATE_IDLE },
    { OTA_FSM_STATE_DOWNLOADING,    OTA_FSM_EVT_DOWNLOAD_FAILED,    action_report_download_failed,        OTA_FSM_STATE_IDLE },
};

static const fsm_table_t OTA_FSM_TABLE = {
    .transitions = OTA_FSM_TRANSITIONS,
    .count       = sizeof(OTA_FSM_TRANSITIONS) / sizeof(OTA_FSM_TRANSITIONS[0]),
};

void ota_fsm_init(ota_fsm_t *ofsm, ota_fsm_state_t initial_state,
                    const ota_fsm_actions_t *actions,
                    fsm_transition_log_fn on_transition, void *log_ctx)
{
    ofsm->actions = *actions;

    fsm_cfg_t cfg = {
        .name          = "ota",
        .table         = &OTA_FSM_TABLE,
        .initial_state = initial_state,
        .on_transition = on_transition,
        .log_ctx       = log_ctx,
        .action_ctx    = ofsm,
    };
    core_fsm_init(&ofsm->fsm, &cfg);
}

fsm_result_t ota_fsm_dispatch(ota_fsm_t *ofsm, ota_fsm_event_t event, const void *event_data)
{
    return core_fsm_dispatch(&ofsm->fsm, event, event_data);
}

ota_fsm_state_t ota_fsm_current_state(const ota_fsm_t *ofsm)
{
    return (ota_fsm_state_t)core_fsm_current_state(&ofsm->fsm);
}
