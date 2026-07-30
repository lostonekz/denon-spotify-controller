/*
 * Health aggregation + fault/coredump retention. docs/MODULES.md,
 * docs/LIFECYCLE.md §2. Phase 1 scope is retention and UART reporting only
 * -- coredump EXPORT (USB/HTTP, pairing PIN) needs a screen (Phase 5) and
 * net_http (Phase 2) that don't exist yet.
 *
 * No dependency on svc_storage/svc_scheduler/svc_logging themselves (the
 * svc layer has no self-edge): storage arrives as an already-open
 * kv_store_if_t, and periodic reporting is driven by whoever wires this up
 * calling svc_diagnostics_report_tick() on a schedule -- same "tick"
 * pattern as svc_settings, for the same reason.
 *
 * The svc_logging RAM ring surviving into a coredump (LIFECYCLE.md §2.2) is
 * a property of where its storage lives (a statically-sized struct field,
 * not a heap allocation) -- not something this component reaches into
 * svc_logging to do.
 */
#ifndef SVC_DIAGNOSTICS_H
#define SVC_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "core_eventbus.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "os_if_types.h"
#include "svc_diagnostics_faultlog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    kv_store_if_t     kv;    /* already open on the "diagnostics" namespace */
    core_eventbus_t  *bus;
    time_source_t      clock;

    uint32_t            fault_seq;      /* monotonic per-boot sequence, not a log index */
    uint32_t            report_count;
} svc_diagnostics_t;

bool svc_diagnostics_create(svc_diagnostics_t *d, core_eventbus_t *bus, kv_store_if_t kv,
                              time_source_t clock);

/* Checks for a coredump image in flash; if one is present and its
 * {task, pc} fingerprint doesn't match the last one already recorded,
 * appends a fault-log entry for it and publishes TOPIC_DIAG. Call once,
 * early in app_wiring. Does not erase the coredump image -- the raw dump
 * is retained for a future export feature (Phase 2+/5+), not consumed
 * here. */
void svc_diagnostics_check_coredump_on_boot(svc_diagnostics_t *d);

/* Records a fault that was caught in software rather than a hardware
 * panic (e.g. a supervisor-detected repeated restart). Appends to the
 * fault log and publishes TOPIC_DIAG. */
void svc_diagnostics_record_fault(svc_diagnostics_t *d, const char *task_name, uint32_t reason);

/* Logs heap (internal + PSRAM) and per-task stack high-water-mark figures
 * over UART. Call periodically (e.g. every few seconds) via a
 * svc_scheduler periodic job posted by app_wiring. */
void svc_diagnostics_report_tick(svc_diagnostics_t *d);

/* Oldest first, same contract as faultlog_load(). */
size_t svc_diagnostics_get_fault_log(svc_diagnostics_t *d, fault_record_t *out, size_t out_capacity);

/* Not in LIFECYCLE.md §1.2's restart table -- defaults to the conservative
 * choice, same reasoning as svc_settings. requires storage: diagnostics'
 * kv_store_if_t (fault log, coredump marker) comes from a namespace
 * svc_storage opens. degraded_ok = true: losing diagnostics loses
 * visibility, not correctness. */
static const service_desc_t SVC_DIAGNOSTICS_SERVICE_DESC = {
    .id           = SVC_ID_DIAGNOSTICS,
    .name         = "diagnostics",
    .requires     = SVC_BIT(SVC_ID_STORAGE),
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

#endif /* SVC_DIAGNOSTICS_H */
