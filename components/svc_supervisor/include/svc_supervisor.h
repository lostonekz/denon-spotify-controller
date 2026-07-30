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
 */
#ifndef SVC_SUPERVISOR_H
#define SVC_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lifecycle_types.h"
#include "os_if_types.h"
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

#ifdef __cplusplus
}
#endif

#endif /* SVC_SUPERVISOR_H */
