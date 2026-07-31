/*
 * SNTP sync. docs/NETWORKING.md §1.1: "SNTP gates everything TLS. Without
 * a valid clock, certificate validation fails with a confusing error."
 *
 * No dependency on net_wifi (the net layer has no self-edge,
 * arch_rules.json): learns WiFi is up by subscribing to TOPIC_NET itself
 * (legal -- core_eventbus is a core dependency), the same pattern net_wifi
 * uses in reverse to learn SNTP has synced (it subscribes to TOPIC_TIME).
 *
 * No dedicated task -- docs/ARCHITECTURE.md §4's task table has no row for
 * net_time, consistent with the Phase 1 "tick" pattern: driven by
 * net_time_tick(), scheduled periodically by whoever wires this up
 * (app_wiring, via svc_scheduler), same as svc_settings/svc_diagnostics.
 */
#ifndef NET_TIME_H
#define NET_TIME_H

#include <stdbool.h>

#include "core_eventbus.h"
#include "lifecycle_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_eventbus_t  *bus;
    const char          *sntp_server;   /* Kconfig DSC_SNTP_SERVER */

    eventbus_sub_h    net_sub;
    event_t             net_sub_storage[4];
    bool                  ip_seen;
    bool                  sntp_started;
    bool                  synced;
} net_time_t;

typedef struct {
    core_eventbus_t  *bus;
    const char          *sntp_server;
} net_time_cfg_t;

bool net_time_create(net_time_t *t, const net_time_cfg_t *cfg);

/* Call periodically (recommended: every ~1 s) via a svc_scheduler periodic
 * job posted by app_wiring. Starts SNTP once TOPIC_NET shows an acquired
 * IP; publishes TOPIC_TIME{synced=true} once, the first time sync
 * completes, then becomes a no-op. */
void net_time_tick(net_time_t *t);

/* Not in LIFECYCLE.md §1.2's restart table -- defaults to the conservative
 * choice, same reasoning as net_wifi/net_http/svc_ota. degraded_ok = true:
 * TLS just can't be validated yet until sync completes, not fatal. */
static const service_desc_t SVC_NET_TIME_SERVICE_DESC = {
    .id           = SVC_ID_NET_TIME,
    .name         = "net_time",
    .requires     = 0,
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

#endif /* NET_TIME_H */
