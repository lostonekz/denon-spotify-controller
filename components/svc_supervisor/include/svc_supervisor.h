/*
 * Runtime lifecycle supervisor -- mechanism, not policy. docs/LIFECYCLE.md
 * §1: executes the boot graph, tracks each subsystem's declared status,
 * and applies whatever restart policy that subsystem's own service_desc_t
 * specifies. It has no per-service knowledge of its own.
 *
 * Deliberately has no dependency on any real svc_* component (the svc
 * layer has no self-edge) and, unlike svc_settings/svc_diagnostics, needs
 * no FreeRTOS/ESP-IDF glue at all: the one non-portable primitive --
 * rebooting the device -- is injected as a plain function pointer
 * (esp_restart on target; a call-counting fake on host), same DI seam as
 * os_lock_if_t/time_source_t. That makes the whole restart mechanism
 * host-testable end to end, not just the pure math underneath it.
 *
 * KNOWN GAP: the device-level reboot cap (below) is RAM-only for Phase 1.
 * An actual reboot wipes it along with the rest of RAM, so it only holds
 * within one power-up session, not across repeated cold boots. Persisting
 * it needs an injected kv_store_if_t and is deliberately deferred -- no
 * Phase 1 exit criterion requires it, and this is a scope cut, not an
 * oversight.
 *
 * Phase 2 adds the OTA health gate (LIFECYCLE.md §1.4: svc_supervisor owns
 * the verdict so svc_ota never reaches into subsystems it should not know
 * about). The gate itself (svc_supervisor_health.h) is pure; this file only
 * adds the glue to feed it: an opt-in subscription to TOPIC_NET/TOPIC_TIME
 * (legal -- core_eventbus is a core dependency, unlike a direct svc_ota
 * edge) for the two conditions observable passively, plus a thin
 * report/verdict API for app_wiring (and, relayed through it, svc_ota) to
 * use for the conditions it cannot observe on its own. svc_supervisor_init()
 * itself is unchanged -- the health gate is only wired up by calling
 * svc_supervisor_health_gate_start() afterward, so every existing caller
 * (and test_supervisor.c) keeps working untouched.
 */
#ifndef SVC_SUPERVISOR_H
#define SVC_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core_eventbus.h"
#include "lifecycle_types.h"
#include "os_if_types.h"
#include "svc_supervisor_health.h"
#include "svc_supervisor_restart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LIFECYCLE.md §1.3's device-level cap: 3 reboot attempts in 10 minutes,
 * then hold rather than loop. */
#define SVC_SUPERVISOR_REBOOT_WINDOW_US    ((int64_t)10 * 60 * 1000000)
#define SVC_SUPERVISOR_REBOOT_MAX_ATTEMPTS 3

/* Per-service max_restarts (RESTART_IMMEDIATE / RESTART_BACKOFF_THEN_DEGRADE)
 * is a lifetime cap, not a sliding window -- LIFECYCLE.md §1.2 describes
 * restart as something "mostly we do not do", with no stated expiry for a
 * service's own attempt count, unlike the reboot cap's explicit "10
 * minutes". */
#define SVC_SUPERVISOR_LIFETIME_WINDOW_US INT64_MAX

typedef struct {
    subsystem_status_t  status;
    void                 *handle;           /* opaque to the supervisor */
    restart_history_t     restart_history;
} svc_state_t;

typedef struct {
    const service_desc_t *descs;    /* caller-owned, count entries */
    svc_state_t            *states;   /* caller-owned, count entries, parallel to descs */
    size_t                   count;

    os_lock_if_t              lock;
    time_source_t               clock;
    void                     (*reboot_fn)(void);

    restart_history_t          reboot_history;   /* see the KNOWN GAP note above */

    /* OTA health gate -- unused (all-zero) unless svc_supervisor_health_gate_start()
     * is called. */
    svc_supervisor_health_t   health;
    core_eventbus_t            *health_bus;
    eventbus_sub_h               health_net_sub;
    event_t                        health_net_sub_storage[4];
    eventbus_sub_h               health_time_sub;
    event_t                        health_time_sub_storage[4];
} svc_supervisor_t;

void svc_supervisor_init(svc_supervisor_t *sup, const service_desc_t *descs, svc_state_t *states,
                           size_t count, os_lock_if_t lock, time_source_t clock,
                           void (*reboot_fn)(void));

/* Validates the boot graph (cycle / missing-dependency detection); if
 * valid, marks every service READY in dependency order and returns true.
 * False, no state changes, if the graph itself is invalid -- a startup
 * assertion, not a runtime decision. Phase 1 has no real per-service
 * _start() failure path yet, so this is the happy-path boot sequence. */
bool svc_supervisor_boot(svc_supervisor_t *sup);

/* False if `id` was never registered (not among descs[]). */
bool svc_supervisor_get_status(const svc_supervisor_t *sup, service_id_t id,
                                 subsystem_status_t *out);

/* A service (or whoever supervises it) reports its own new status. FAILED
 * triggers the restart-policy decision: RESTART_REBOOT reboots the device,
 * subject to the device-level cap; RESTART_IMMEDIATE and
 * RESTART_BACKOFF_THEN_DEGRADE run teardown then restart_fn, subject to
 * the service's own max_restarts, degrading (if degraded_ok) or failing
 * permanently once that cap is hit. No-op if `id` was never registered. */
void svc_supervisor_report_state(svc_supervisor_t *sup, service_id_t id, subsystem_status_t status);

/* Starts the OTA health gate: inits svc_supervisor_health_t against sup's
 * own clock, and subscribes to TOPIC_NET/TOPIC_TIME for the two conditions
 * (HEALTH_COND_WIFI_IP, HEALTH_COND_SNTP_SYNCED) this module can observe
 * passively. Call once, after svc_supervisor_init(), only on a build that
 * actually has net_wifi/net_time wired up (app_wiring's job, not this
 * module's -- svc_supervisor itself has no opinion on whether OTA exists).
 */
void svc_supervisor_health_gate_start(svc_supervisor_t *sup, core_eventbus_t *bus);

/* Drains any queued TOPIC_NET/TOPIC_TIME events since the last call and
 * folds them into the health gate. Call periodically (recommended: every
 * ~1 s), same tick pattern as net_time_tick()/svc_settings_tick(). No-op if
 * svc_supervisor_health_gate_start() was never called. */
void svc_supervisor_health_gate_tick(svc_supervisor_t *sup);

/* For the conditions this module cannot observe on its own (boot-complete,
 * storage-mounted, TLS-handshake-proven, heap-watermark, plus the Phase 2
 * panel/UI and tasks-ok stand-ins) -- reported by app_wiring or (relayed
 * through app_wiring, since svc has no self-edge) svc_ota. Sticky, same as
 * svc_supervisor_health_report() underneath. Named distinctly from that
 * function (not an overload -- C has none) to avoid a same-name/
 * different-signature collision with svc_supervisor_health.h. */
void svc_supervisor_report_health_condition(svc_supervisor_t *sup, health_condition_t cond, bool ok);

/* The current verdict, evaluated against sup's own clock. Meaningful only
 * after svc_supervisor_health_gate_start() has been called; calling it
 * beforehand reads whatever clock.now_us() returns against a start_us of
 * 0, which is not a real gate window and is the caller's bug to avoid. */
health_gate_verdict_t svc_supervisor_health_gate_verdict(svc_supervisor_t *sup);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SUPERVISOR_H */
