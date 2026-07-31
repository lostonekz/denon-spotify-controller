#include "net_wifi_fsm.h"

#include <stddef.h>

static net_wifi_fsm_actions_t *actions_of(void *ctx)
{
    net_wifi_fsm_t *nfsm = (net_wifi_fsm_t *)ctx;
    return &nfsm->actions;
}

static void action_start_provisioning(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->start_provisioning != NULL) a->start_provisioning(a->ctx);
}

static void action_start_connect(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->start_connect != NULL) a->start_connect(a->ctx);
}

static void action_enter_syncing(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->enter_syncing != NULL) a->enter_syncing(a->ctx);
}

static void action_enter_online(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->enter_online != NULL) a->enter_online(a->ctx);
}

static void action_enter_degraded(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->enter_degraded != NULL) a->enter_degraded(a->ctx);
}

static void action_start_backoff(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    net_wifi_fsm_actions_t *a = actions_of(ctx);
    if (a->start_backoff != NULL) a->start_backoff(a->ctx);
}

static const fsm_transition_t NET_WIFI_TRANSITIONS[] = {
    { NET_WIFI_STATE_BOOT,          NET_WIFI_EVT_NO_CREDENTIALS,      action_start_provisioning, NET_WIFI_STATE_PROVISIONING },
    { NET_WIFI_STATE_BOOT,          NET_WIFI_EVT_HAVE_CREDENTIALS,    action_start_connect,      NET_WIFI_STATE_CONNECTING },
    { NET_WIFI_STATE_PROVISIONING,  NET_WIFI_EVT_CREDENTIALS_SAVED,   action_start_connect,      NET_WIFI_STATE_CONNECTING },
    { NET_WIFI_STATE_CONNECTING,    NET_WIFI_EVT_GOT_IP,              action_enter_syncing,      NET_WIFI_STATE_SYNCING },
    { NET_WIFI_STATE_CONNECTING,    NET_WIFI_EVT_CONNECT_FAILED,      action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    { NET_WIFI_STATE_SYNCING,       NET_WIFI_EVT_SNTP_OK,             action_enter_online,       NET_WIFI_STATE_ONLINE },
    { NET_WIFI_STATE_SYNCING,       NET_WIFI_EVT_SNTP_TIMEOUT,        action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    /* The link can drop mid-sync too, not just time out -- the glue maps
     * any WIFI_EVENT_STA_DISCONNECTED while SYNCING to this same event. */
    { NET_WIFI_STATE_SYNCING,       NET_WIFI_EVT_LINK_LOST,           action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    { NET_WIFI_STATE_ONLINE,        NET_WIFI_EVT_LINK_LOST,           action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    { NET_WIFI_STATE_ONLINE,        NET_WIFI_EVT_DNS_FAIL,            action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    { NET_WIFI_STATE_ONLINE,        NET_WIFI_EVT_API_ERRORS_EXCEEDED, action_enter_degraded,     NET_WIFI_STATE_DEGRADED },
    { NET_WIFI_STATE_DEGRADED,      NET_WIFI_EVT_BEGIN_RECONNECT,     action_start_backoff,      NET_WIFI_STATE_RECONNECTING },
    { NET_WIFI_STATE_RECONNECTING,  NET_WIFI_EVT_RETRY_NOW,           action_start_connect,      NET_WIFI_STATE_CONNECTING },
};

static const fsm_table_t NET_WIFI_TABLE = {
    .transitions = NET_WIFI_TRANSITIONS,
    .count       = sizeof(NET_WIFI_TRANSITIONS) / sizeof(NET_WIFI_TRANSITIONS[0]),
};

void net_wifi_fsm_init(net_wifi_fsm_t *nfsm, const net_wifi_fsm_actions_t *actions,
                          fsm_transition_log_fn on_transition, void *log_ctx)
{
    nfsm->actions = *actions;

    fsm_cfg_t cfg = {
        .name          = "net_wifi",
        .table         = &NET_WIFI_TABLE,
        .initial_state = NET_WIFI_STATE_BOOT,
        .on_transition = on_transition,
        .log_ctx       = log_ctx,
        .action_ctx    = nfsm,
    };
    core_fsm_init(&nfsm->fsm, &cfg);
}

fsm_result_t net_wifi_fsm_dispatch(net_wifi_fsm_t *nfsm, net_wifi_event_t event,
                                      const void *event_data)
{
    fsm_result_t r = core_fsm_dispatch(&nfsm->fsm, event, event_data);

    if (r == FSM_OK && core_fsm_current_state(&nfsm->fsm) == NET_WIFI_STATE_DEGRADED) {
        core_fsm_dispatch(&nfsm->fsm, NET_WIFI_EVT_BEGIN_RECONNECT, NULL);
    }

    return r;
}

net_wifi_state_t net_wifi_fsm_current_state(const net_wifi_fsm_t *nfsm)
{
    return (net_wifi_state_t)core_fsm_current_state(&nfsm->fsm);
}
