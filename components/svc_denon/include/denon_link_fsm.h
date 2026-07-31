/*
 * The Denon AVR link FSM. docs/EVENTS_AND_STATE.md §6.4 explicitly lists
 * denon_link among the FSMs that must be table-driven on core_fsm_t, and
 * uses "[fsm:denon_link] CONNECTED --(EVT_SOCKET_EOF)--> BACKOFF" as its
 * own worked example. Built on core_fsm_t exactly like net_wifi_fsm.c/
 * ota_fsm.c: side effects injected via denon_link_fsm_actions_t, host tests
 * supply call-counting fakes and assert the exact transition sequence.
 *
 * docs/adr/0003's 4-point strategy, as states and events:
 *   1. Try telnet first.               CONNECTING_TELNET
 *   2. Refusal/EOF -> HTTP immediately  CONNECTING_TELNET/CONNECTED_TELNET
 *      (never through backoff).        --(TELNET_REFUSED/TELNET_EOF)--> CONNECTING_HTTP
 *   3. Retry telnet every 60s+jitter;   CONNECTED_HTTP --(PROMOTE_DUE)--> CONNECTING_TELNET
 *      promote back on success.
 *   4. Release the socket while         CONNECTED_TELNET/CONNECTED_HTTP
 *      asleep and the AVR is off.       --(SLEEP_ENTER)--> RELEASED
 *
 * BACKOFF is two states (_DISCOVERY / _CONNECT), not one: core_fsm_t is
 * purely (state,event)->(action,next_state) table-driven -- one row cannot
 * pick a different next_state depending on how backoff was entered.
 * Splitting the state keeps that decision in the table instead of hiding a
 * branch inside an action function. BACKOFF_DISCOVERY is entered only when
 * discovery itself fails (no host known at all); BACKOFF_CONNECT only when
 * both transports have failed against an already-known host.
 *
 * The capability probe (NETWORKING.md §1.4's Deviceinfo.xml, startup-time
 * and transport-independent) fires once per newly-confirmed host --
 * attached to the two transitions that mean "we just learned/confirmed
 * this host" (HOST_CONFIGURED, DISCOVERED) -- not on routine reconnects
 * (promotion, backoff-elapsed, wake), since the device's own capabilities
 * don't change between those.
 *
 * SLEEP_ENTER/WAKE have no real producer until Phase 6's svc_power --
 * kept in the table now to avoid Phase 6 rework, exercised only
 * synthetically in the host test. Same treatment net_wifi_fsm.h gives
 * NET_WIFI_EVT_DNS_FAIL: a real row, explicitly flagged, not silently dead.
 */
#ifndef DENON_LINK_FSM_H
#define DENON_LINK_FSM_H

#include <stdint.h>

#include "core_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DENON_LINK_STATE_IDLE = 0,
    DENON_LINK_STATE_DISCOVERING,
    DENON_LINK_STATE_CONNECTING_TELNET,
    DENON_LINK_STATE_CONNECTED_TELNET,
    DENON_LINK_STATE_CONNECTING_HTTP,
    DENON_LINK_STATE_CONNECTED_HTTP,
    DENON_LINK_STATE_BACKOFF_DISCOVERY,
    DENON_LINK_STATE_BACKOFF_CONNECT,
    DENON_LINK_STATE_RELEASED,
} denon_link_state_t;

typedef enum {
    DENON_LINK_EVT_HOST_CONFIGURED = 0,   /* cached/manually-set host at boot or via the console */
    DENON_LINK_EVT_DISCOVERY_NEEDED,      /* no host known */
    DENON_LINK_EVT_DISCOVERED,
    DENON_LINK_EVT_DISCOVERY_FAILED,
    DENON_LINK_EVT_TELNET_CONNECTED,
    DENON_LINK_EVT_TELNET_REFUSED,
    DENON_LINK_EVT_TELNET_EOF,
    DENON_LINK_EVT_HTTP_CONNECTED,        /* capability probe / connect via HTTP fallback ok */
    DENON_LINK_EVT_HTTP_FAILED,
    DENON_LINK_EVT_HOST_UNREACHABLE,      /* cached host stopped responding -- re-discover */
    DENON_LINK_EVT_PROMOTE_DUE,           /* 60s+jitter timer, glue-owned */
    DENON_LINK_EVT_BACKOFF_ELAPSED,       /* glue-owned backoff timer */
    DENON_LINK_EVT_SLEEP_ENTER,           /* no real producer until Phase 6 -- see header comment */
    DENON_LINK_EVT_WAKE,                  /* no real producer until Phase 6 -- see header comment */
} denon_link_event_t;

typedef struct {
    void (*start_discovery)(void *ctx);
    void (*start_connect_telnet)(void *ctx);
    void (*probe_capabilities)(void *ctx);     /* fired alongside start_connect_telnet -- see header comment */
    void (*enter_connected_telnet)(void *ctx);
    void (*start_connect_http)(void *ctx);
    void (*enter_connected_http)(void *ctx);
    void (*start_backoff_discovery)(void *ctx);
    void (*start_backoff_connect)(void *ctx);
    void (*release_link)(void *ctx);
    void *ctx;
} denon_link_fsm_actions_t;

typedef struct {
    core_fsm_t                  fsm;
    denon_link_fsm_actions_t      actions;
} denon_link_fsm_t;

/* Caller-chosen initial_state (same shape as ota_fsm_init, not
 * net_wifi_fsm_init's fixed BOOT): boot decides IDLE vs CONNECTING_TELNET
 * based on whether SETTING_DENON_HOST is already cached. */
void denon_link_fsm_init(denon_link_fsm_t *dfsm, denon_link_state_t initial_state,
                            const denon_link_fsm_actions_t *actions,
                            fsm_transition_log_fn on_transition, void *log_ctx);

fsm_result_t denon_link_fsm_dispatch(denon_link_fsm_t *dfsm, denon_link_event_t event,
                                        const void *event_data);

denon_link_state_t denon_link_fsm_current_state(const denon_link_fsm_t *dfsm);

#ifdef __cplusplus
}
#endif

#endif /* DENON_LINK_FSM_H */
