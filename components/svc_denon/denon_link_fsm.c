#include "denon_link_fsm.h"

#include <stddef.h>

static denon_link_fsm_actions_t *actions_of(void *ctx)
{
    denon_link_fsm_t *dfsm = (denon_link_fsm_t *)ctx;
    return &dfsm->actions;
}

static void action_start_discovery(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_discovery != NULL) a->start_discovery(a->ctx);
}

static void action_start_connect_telnet(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_connect_telnet != NULL) a->start_connect_telnet(a->ctx);
}

/* Used for the two transitions that mean "we just learned/confirmed this
 * host" -- HOST_CONFIGURED and DISCOVERED. Not used for routine reconnects
 * (promotion, backoff-elapsed, wake), which only call start_connect_telnet. */
static void action_connect_telnet_and_probe(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_connect_telnet != NULL) a->start_connect_telnet(a->ctx);
    if (a->probe_capabilities != NULL)   a->probe_capabilities(a->ctx);
}

static void action_enter_connected_telnet(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->enter_connected_telnet != NULL) a->enter_connected_telnet(a->ctx);
}

static void action_start_connect_http(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_connect_http != NULL) a->start_connect_http(a->ctx);
}

static void action_enter_connected_http(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->enter_connected_http != NULL) a->enter_connected_http(a->ctx);
}

static void action_start_backoff_discovery(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_backoff_discovery != NULL) a->start_backoff_discovery(a->ctx);
}

static void action_start_backoff_connect(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->start_backoff_connect != NULL) a->start_backoff_connect(a->ctx);
}

static void action_release_link(void *ctx, uint32_t event, const void *data)
{
    (void)event; (void)data;
    denon_link_fsm_actions_t *a = actions_of(ctx);
    if (a->release_link != NULL) a->release_link(a->ctx);
}

static const fsm_transition_t DENON_LINK_TRANSITIONS[] = {
    { DENON_LINK_STATE_IDLE,             DENON_LINK_EVT_HOST_CONFIGURED,  action_connect_telnet_and_probe, DENON_LINK_STATE_CONNECTING_TELNET },
    { DENON_LINK_STATE_IDLE,             DENON_LINK_EVT_DISCOVERY_NEEDED, action_start_discovery,          DENON_LINK_STATE_DISCOVERING },
    { DENON_LINK_STATE_DISCOVERING,      DENON_LINK_EVT_DISCOVERED,       action_connect_telnet_and_probe, DENON_LINK_STATE_CONNECTING_TELNET },
    { DENON_LINK_STATE_DISCOVERING,      DENON_LINK_EVT_DISCOVERY_FAILED, action_start_backoff_discovery,  DENON_LINK_STATE_BACKOFF_DISCOVERY },

    { DENON_LINK_STATE_CONNECTING_TELNET, DENON_LINK_EVT_TELNET_CONNECTED, action_enter_connected_telnet,  DENON_LINK_STATE_CONNECTED_TELNET },
    /* Immediate fallback, never through backoff -- adr/0003 point 2. */
    { DENON_LINK_STATE_CONNECTING_TELNET, DENON_LINK_EVT_TELNET_REFUSED,   action_start_connect_http,      DENON_LINK_STATE_CONNECTING_HTTP },
    { DENON_LINK_STATE_CONNECTED_TELNET,  DENON_LINK_EVT_TELNET_EOF,       action_start_connect_http,      DENON_LINK_STATE_CONNECTING_HTTP },

    { DENON_LINK_STATE_CONNECTING_HTTP,  DENON_LINK_EVT_HTTP_CONNECTED,   action_enter_connected_http,     DENON_LINK_STATE_CONNECTED_HTTP },
    { DENON_LINK_STATE_CONNECTING_HTTP,  DENON_LINK_EVT_HTTP_FAILED,      action_start_backoff_connect,    DENON_LINK_STATE_BACKOFF_CONNECT },

    /* "Re-discovery only when the cached host stops responding" -- NETWORKING.md §1.4. */
    { DENON_LINK_STATE_CONNECTED_HTTP,   DENON_LINK_EVT_HOST_UNREACHABLE, action_start_discovery,          DENON_LINK_STATE_DISCOVERING },
    /* 60s+jitter promotion attempt -- adr/0003 point 3. No re-probe: same
     * host we already know, not a newly-confirmed one. */
    { DENON_LINK_STATE_CONNECTED_HTTP,   DENON_LINK_EVT_PROMOTE_DUE,      action_start_connect_telnet,     DENON_LINK_STATE_CONNECTING_TELNET },

    { DENON_LINK_STATE_BACKOFF_DISCOVERY, DENON_LINK_EVT_BACKOFF_ELAPSED, action_start_discovery,          DENON_LINK_STATE_DISCOVERING },
    { DENON_LINK_STATE_BACKOFF_CONNECT,   DENON_LINK_EVT_BACKOFF_ELAPSED, action_start_connect_telnet,     DENON_LINK_STATE_CONNECTING_TELNET },

    /* adr/0003 point 4 -- release the socket while asleep and the AVR is off. */
    { DENON_LINK_STATE_CONNECTED_TELNET, DENON_LINK_EVT_SLEEP_ENTER,      action_release_link,             DENON_LINK_STATE_RELEASED },
    { DENON_LINK_STATE_CONNECTED_HTTP,   DENON_LINK_EVT_SLEEP_ENTER,      action_release_link,             DENON_LINK_STATE_RELEASED },
    { DENON_LINK_STATE_RELEASED,         DENON_LINK_EVT_WAKE,             action_start_connect_telnet,     DENON_LINK_STATE_CONNECTING_TELNET },
};

static const fsm_table_t DENON_LINK_TABLE = {
    .transitions = DENON_LINK_TRANSITIONS,
    .count       = sizeof(DENON_LINK_TRANSITIONS) / sizeof(DENON_LINK_TRANSITIONS[0]),
};

void denon_link_fsm_init(denon_link_fsm_t *dfsm, denon_link_state_t initial_state,
                            const denon_link_fsm_actions_t *actions,
                            fsm_transition_log_fn on_transition, void *log_ctx)
{
    dfsm->actions = *actions;

    fsm_cfg_t cfg = {
        .name          = "denon_link",
        .table         = &DENON_LINK_TABLE,
        .initial_state = initial_state,
        .on_transition = on_transition,
        .log_ctx       = log_ctx,
        .action_ctx    = dfsm,
    };
    core_fsm_init(&dfsm->fsm, &cfg);
}

fsm_result_t denon_link_fsm_dispatch(denon_link_fsm_t *dfsm, denon_link_event_t event,
                                        const void *event_data)
{
    return core_fsm_dispatch(&dfsm->fsm, event, event_data);
}

denon_link_state_t denon_link_fsm_current_state(const denon_link_fsm_t *dfsm)
{
    return (denon_link_state_t)core_fsm_current_state(&dfsm->fsm);
}
