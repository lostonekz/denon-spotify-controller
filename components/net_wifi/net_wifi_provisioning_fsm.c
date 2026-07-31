#include "net_wifi_provisioning_fsm.h"

#include <stddef.h>

static net_wifi_prov_actions_t *actions_of(void *ctx)
{
    net_wifi_prov_fsm_t *pfsm = (net_wifi_prov_fsm_t *)ctx;
    return &pfsm->actions;
}

static void action_start_ap(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->start_ap != NULL) a->start_ap(a->ctx);
}

static void action_ap_active(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->ap_active != NULL) a->ap_active(a->ctx);
}

static void action_begin_save(void *ctx, uint32_t event, const void *data)
{
    (void)event;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->begin_save != NULL) a->begin_save(a->ctx, data);
}

static void action_save_failed(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->save_failed != NULL) a->save_failed(a->ctx);
}

static void action_stop_ap(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->stop_ap != NULL) a->stop_ap(a->ctx);
}

static void action_done(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->done != NULL) a->done(a->ctx);
}

static void action_failed(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_prov_actions_t *a = actions_of(ctx);
    if (a->failed != NULL) a->failed(a->ctx);
}

static const fsm_transition_t NET_WIFI_PROV_TRANSITIONS[] = {
    { PROV_STATE_IDLE,         PROV_EVT_START,                 action_start_ap,   PROV_STATE_AP_STARTING },
    { PROV_STATE_AP_STARTING,  PROV_EVT_AP_UP,                 action_ap_active,  PROV_STATE_AP_ACTIVE },
    { PROV_STATE_AP_STARTING,  PROV_EVT_AP_START_FAILED,       action_failed,     PROV_STATE_FAILED },
    { PROV_STATE_AP_ACTIVE,    PROV_EVT_CREDENTIALS_SUBMITTED, action_begin_save, PROV_STATE_SAVING },
    { PROV_STATE_SAVING,       PROV_EVT_SAVE_OK,               action_stop_ap,    PROV_STATE_AP_STOPPING },
    { PROV_STATE_SAVING,       PROV_EVT_SAVE_FAILED,           action_save_failed, PROV_STATE_AP_ACTIVE },
    { PROV_STATE_AP_STOPPING,  PROV_EVT_AP_STOPPED,            action_done,       PROV_STATE_DONE },
};

static const fsm_table_t NET_WIFI_PROV_TABLE = {
    .transitions = NET_WIFI_PROV_TRANSITIONS,
    .count       = sizeof(NET_WIFI_PROV_TRANSITIONS) / sizeof(NET_WIFI_PROV_TRANSITIONS[0]),
};

void net_wifi_prov_fsm_init(net_wifi_prov_fsm_t *pfsm, const net_wifi_prov_actions_t *actions,
                              fsm_transition_log_fn on_transition, void *log_ctx)
{
    pfsm->actions = *actions;

    fsm_cfg_t cfg = {
        .name          = "provisioning",
        .table         = &NET_WIFI_PROV_TABLE,
        .initial_state = PROV_STATE_IDLE,
        .on_transition = on_transition,
        .log_ctx       = log_ctx,
        .action_ctx    = pfsm,
    };
    core_fsm_init(&pfsm->fsm, &cfg);
}

fsm_result_t net_wifi_prov_fsm_dispatch(net_wifi_prov_fsm_t *pfsm, net_wifi_prov_event_t event,
                                           const void *event_data)
{
    return core_fsm_dispatch(&pfsm->fsm, event, event_data);
}

net_wifi_prov_state_t net_wifi_prov_fsm_current_state(const net_wifi_prov_fsm_t *pfsm)
{
    return (net_wifi_prov_state_t)core_fsm_current_state(&pfsm->fsm);
}
