/*
 * The connectivity FSM. docs/NETWORKING.md §1.1:
 *
 *   BOOT --no credentials--> PROVISIONING --saved--> CONNECTING
 *   BOOT --credentials-----------------------------> CONNECTING
 *   CONNECTING --got IP--> SYNCING --SNTP ok--> ONLINE
 *   ONLINE --link lost / DNS fail / N consecutive API errors--> DEGRADED
 *   DEGRADED --> RECONNECTING --> (back to CONNECTING)
 *
 * Built on Phase 1's core_fsm_t (EVENTS_AND_STATE.md §6.4 explicitly lists
 * net_wifi among the FSMs that must be table-driven). Side effects are
 * injected via net_wifi_fsm_actions_t, the same "inject the one
 * non-portable bit" pattern svc_supervisor.c proved out for its reboot_fn:
 * host tests supply call-counting fakes and assert the exact transition
 * sequence.
 *
 * Two transitions the source diagram draws as a single unlabeled arrow are
 * modeled as two real states with an automatic follow-on, not one state
 * wearing two names: DEGRADED (a status the UI can report as STALE the
 * instant connectivity is lost) and RECONNECTING (the backoff/retry
 * attempt actually in flight) mean different things to
 * net_state_t.meta.status (LINK_STATUS_STALE vs LINK_STATUS_RECONNECTING,
 * both already in core_types). net_wifi_fsm_dispatch() (not
 * core_fsm_dispatch() directly) is what makes the DEGRADED->RECONNECTING
 * hop automatic -- callers only ever dispatch the event that caused the
 * degradation, once.
 *
 * Three transitions the diagram doesn't draw at all are added to actually
 * close the retry loop it implies: CONNECTING and SYNCING both need a
 * failure/timeout path back to DEGRADED (CONNECT_FAILED, SNTP_TIMEOUT), or
 * a stuck connection attempt or a firewalled SNTP server would leave the
 * device parked forever with no retry; SYNCING also needs its own
 * LINK_LOST entry, since the link can drop mid-sync, not just time out.
 * Documented here, not silently invented: the diagram shows the happy path
 * and the already-degraded recovery path; it does not show every way into
 * the second one.
 */
#ifndef NET_WIFI_FSM_H
#define NET_WIFI_FSM_H

#include <stdint.h>

#include "core_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_WIFI_STATE_BOOT = 0,
    NET_WIFI_STATE_PROVISIONING,
    NET_WIFI_STATE_CONNECTING,
    NET_WIFI_STATE_SYNCING,
    NET_WIFI_STATE_ONLINE,
    NET_WIFI_STATE_DEGRADED,
    NET_WIFI_STATE_RECONNECTING,
} net_wifi_state_t;

typedef enum {
    NET_WIFI_EVT_NO_CREDENTIALS = 0,
    NET_WIFI_EVT_HAVE_CREDENTIALS,
    NET_WIFI_EVT_CREDENTIALS_SAVED,
    NET_WIFI_EVT_GOT_IP,
    NET_WIFI_EVT_CONNECT_FAILED,
    NET_WIFI_EVT_SNTP_OK,
    NET_WIFI_EVT_SNTP_TIMEOUT,
    NET_WIFI_EVT_LINK_LOST,
    NET_WIFI_EVT_DNS_FAIL,             /* no real producer yet -- kept open, NETWORKING.md §1.1 */
    NET_WIFI_EVT_API_ERRORS_EXCEEDED,  /* no real producer yet -- kept open, NETWORKING.md §1.1 */
    NET_WIFI_EVT_BEGIN_RECONNECT,      /* internal -- fired automatically, see net_wifi_fsm_dispatch() */
    NET_WIFI_EVT_RETRY_NOW,            /* fired by the glue once its backoff timer elapses */
} net_wifi_event_t;

typedef struct {
    void (*start_provisioning)(void *ctx);
    void (*start_connect)(void *ctx);
    void (*enter_syncing)(void *ctx);
    void (*enter_online)(void *ctx);
    void (*enter_degraded)(void *ctx);   /* report STALE; net_wifi_fsm_dispatch chains BEGIN_RECONNECT next */
    void (*start_backoff)(void *ctx);    /* report RECONNECTING; arm the retry timer */
    void *ctx;
} net_wifi_fsm_actions_t;

typedef struct {
    core_fsm_t                fsm;
    net_wifi_fsm_actions_t      actions;
} net_wifi_fsm_t;

void net_wifi_fsm_init(net_wifi_fsm_t *nfsm, const net_wifi_fsm_actions_t *actions,
                          fsm_transition_log_fn on_transition, void *log_ctx);

/* Wraps core_fsm_dispatch(): if the dispatched event lands the FSM in
 * DEGRADED, immediately (synchronously, before returning) also dispatches
 * NET_WIFI_EVT_BEGIN_RECONNECT, so the caller ends up observing
 * RECONNECTING with both enter_degraded and start_backoff already having
 * fired, in order, from a single external event. */
fsm_result_t net_wifi_fsm_dispatch(net_wifi_fsm_t *nfsm, net_wifi_event_t event,
                                      const void *event_data);

net_wifi_state_t net_wifi_fsm_current_state(const net_wifi_fsm_t *nfsm);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_FSM_H */
