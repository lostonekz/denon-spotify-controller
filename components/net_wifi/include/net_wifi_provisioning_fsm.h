/*
 * The SoftAP provisioning portal's own FSM, nested inside net_wifi_fsm's
 * PROVISIONING state (docs/EVENTS_AND_STATE.md §6.4 lists net_wifi and
 * provisioning as separate FSMs). docs/NETWORKING.md §1.2 stage 1: "Device
 * shows an on-screen QR code and AP name -> phone joins the device AP ->
 * captive portal lists scanned networks -> credentials saved -> SoftAP
 * torn down -> device joins the home LAN."
 *
 *   IDLE --start--> AP_STARTING --up--> AP_ACTIVE --submitted--> SAVING
 *   SAVING --ok--> AP_STOPPING --stopped--> DONE
 *   SAVING --failed--> AP_ACTIVE (retry, portal stays up)
 *   AP_STARTING --failed--> FAILED
 *
 * Unlike net_wifi_fsm_dispatch(), dispatch() here is a plain passthrough --
 * no state needs an automatic follow-on chain. Reporting the outcome of
 * `begin_save` (a real NVS write can succeed or fail) needs a SEPARATE,
 * LATER dispatch call from the caller, never a recursive one from inside
 * the action itself: core_fsm_dispatch() runs the action *before*
 * committing the new state (core_fsm.h's own documented order), so a
 * dispatch made from inside begin_save would still see the OLD state
 * (AP_ACTIVE, not SAVING) and silently find no matching transition. The
 * real sequence is two independent top-level calls:
 *   net_wifi_prov_fsm_dispatch(pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, creds);
 *   bool ok = net_wifi_credentials_save(kv, ssid, psk);   // the real work
 *   net_wifi_prov_fsm_dispatch(pfsm, ok ? PROV_EVT_SAVE_OK : PROV_EVT_SAVE_FAILED, NULL);
 * `begin_save` itself is therefore just a notification (log/report), not
 * the place the real save happens.
 *
 * No auto-timeout on AP_ACTIVE in Phase 2, deliberately: SECURITY.md §5's
 * stated 5-minute listener timeout is for the *OAuth* stage (Phase 4), not
 * named for SoftAP stage 1 here -- a device with nobody nearby to provision
 * it yet has no reason to give up.
 */
#ifndef NET_WIFI_PROVISIONING_FSM_H
#define NET_WIFI_PROVISIONING_FSM_H

#include "core_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROV_STATE_IDLE = 0,
    PROV_STATE_AP_STARTING,
    PROV_STATE_AP_ACTIVE,
    PROV_STATE_SAVING,
    PROV_STATE_AP_STOPPING,
    PROV_STATE_DONE,
    PROV_STATE_FAILED,
} net_wifi_prov_state_t;

typedef enum {
    PROV_EVT_START = 0,
    PROV_EVT_AP_UP,
    PROV_EVT_AP_START_FAILED,
    PROV_EVT_CREDENTIALS_SUBMITTED,
    PROV_EVT_SAVE_OK,
    PROV_EVT_SAVE_FAILED,
    PROV_EVT_AP_STOPPED,
} net_wifi_prov_event_t;

typedef struct {
    void (*start_ap)(void *ctx);                             /* IDLE->AP_STARTING */
    void (*ap_active)(void *ctx);                             /* AP_STARTING->AP_ACTIVE */
    void (*begin_save)(void *ctx, const void *credentials);   /* AP_ACTIVE->SAVING */
    void (*save_failed)(void *ctx);                           /* SAVING->AP_ACTIVE (retry) */
    void (*stop_ap)(void *ctx);                               /* SAVING->AP_STOPPING */
    void (*done)(void *ctx);                                  /* AP_STOPPING->DONE */
    void (*failed)(void *ctx);                                /* AP_STARTING->FAILED */
    void *ctx;
} net_wifi_prov_actions_t;

typedef struct {
    core_fsm_t                fsm;
    net_wifi_prov_actions_t     actions;
} net_wifi_prov_fsm_t;

void net_wifi_prov_fsm_init(net_wifi_prov_fsm_t *pfsm, const net_wifi_prov_actions_t *actions,
                              fsm_transition_log_fn on_transition, void *log_ctx);

fsm_result_t net_wifi_prov_fsm_dispatch(net_wifi_prov_fsm_t *pfsm, net_wifi_prov_event_t event,
                                           const void *event_data);

net_wifi_prov_state_t net_wifi_prov_fsm_current_state(const net_wifi_prov_fsm_t *pfsm);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_PROVISIONING_FSM_H */
