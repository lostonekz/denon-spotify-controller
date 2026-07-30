/*
 * One shared low-priority worker task for deferred/periodic work
 * (docs/MODULES.md), so svc_settings' write-coalescing debounce and
 * svc_diagnostics' periodic UART report don't each spawn their own task.
 *
 * Target-only glue around svc_scheduler_jobs.c's pure due-time logic: real
 * FreeRTOS task + semaphores, not host-tested (the pure logic underneath
 * is, in test_scheduler_jobs.c).
 */
#ifndef SVC_SCHEDULER_H
#define SVC_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lifecycle_types.h"
#include "os_if_types.h"
#include "svc_scheduler_jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_SCHEDULER_MAX_JOBS 16

typedef struct {
    scheduler_job_t        job_storage[SVC_SCHEDULER_MAX_JOBS];
    scheduler_job_table_t  table;
    time_source_t           clock;
    SemaphoreHandle_t        lock_sem;
    SemaphoreHandle_t        wake_sem;
    TaskHandle_t             task_handle;
} svc_scheduler_t;

/* Spawns the shared worker task. False if a semaphore or the task itself
 * fails to create (checked explicitly -- an unchecked task-creation failure
 * that's later assumed running is exactly the R1 spike bug this project
 * already fixed once). */
bool svc_scheduler_create(svc_scheduler_t *s, time_source_t clock);

/* Runs fn(ctx) once, delay_us from now. */
bool svc_scheduler_post_once(svc_scheduler_t *s, int64_t delay_us,
                               scheduler_job_fn fn, void *ctx, scheduler_job_h *out);

/* Runs fn(ctx) every period_us, starting period_us from now. */
bool svc_scheduler_post_periodic(svc_scheduler_t *s, int64_t period_us,
                                   scheduler_job_fn fn, void *ctx, scheduler_job_h *out);

/* No-op on a stale or unknown handle. */
void svc_scheduler_cancel(svc_scheduler_t *s, scheduler_job_h job);

/* Not in LIFECYCLE.md §1.2's restart table -- defaults to the conservative
 * choice ("where containment cannot be proven, reboot instead") since no
 * teardown/restart_fn exists for the worker task. degraded_ok = true: a
 * scheduler failure delays debounced writes and periodic reports, it
 * doesn't itself corrupt anything. */
static const service_desc_t SVC_SCHEDULER_SERVICE_DESC = {
    .id           = SVC_ID_SCHEDULER,
    .name         = "scheduler",
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

#endif /* SVC_SCHEDULER_H */
