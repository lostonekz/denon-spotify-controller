/*
 * Pure due-time bookkeeping for svc_scheduler: given a fixed table of jobs
 * and an injected "now", decide what's due and reschedule periodics.
 * No FreeRTOS, no I/O -- directly host-testable, same as core_fsm.
 *
 * Handles are generation-tagged ({index, generation}, bumped on post only)
 * rather than built on core_pool_slot: the worker task must scan every
 * active job each tick to find the earliest due one, and core_pool_slot has
 * no enumeration API for that (by design -- it's for a caller resolving its
 * OWN handle, not for the owner walking everything it holds). Same call as
 * core_eventbus's subscriber table, for the same reason.
 */
#ifndef SVC_SCHEDULER_JOBS_H
#define SVC_SCHEDULER_JOBS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*scheduler_job_fn)(void *ctx);

typedef struct {
    uint32_t index;
    uint32_t generation;
} scheduler_job_h;

typedef struct {
    bool             in_use;
    uint32_t         generation;   /* bumped on post only; 0 is reserved invalid */
    int64_t          due_us;
    int64_t          period_us;    /* 0 = one-shot */
    scheduler_job_fn fn;
    void            *ctx;
} scheduler_job_t;

typedef struct {
    scheduler_job_t *jobs;         /* caller-owned, capacity entries */
    uint32_t          capacity;
} scheduler_job_table_t;

void scheduler_job_table_init(scheduler_job_table_t *t, scheduler_job_t *storage, uint32_t capacity);

/* False (out untouched) if the table is full. */
bool scheduler_job_table_post(scheduler_job_table_t *t, int64_t now_us, int64_t delay_us,
                                int64_t period_us, scheduler_job_fn fn, void *ctx,
                                scheduler_job_h *out);

/* No-op on a stale or unknown handle. */
void scheduler_job_table_cancel(scheduler_job_table_t *t, scheduler_job_h h);

/* If any in_use job's due_us <= now_us, fills out_fn and out_ctx for the
 * EARLIEST such job, reschedules it (due_us += period_us, snapped forward
 * to now_us + period_us if that would still be <= now_us -- a worker that
 * falls behind skips a catch-up burst rather than firing it) if periodic,
 * or frees its slot if one-shot, and returns true. Does not call the
 * callback itself, so this stays pure. False (out untouched) if nothing is
 * due. Call in a loop until it returns false to drain everything due at
 * a given `now_us`. */
bool scheduler_job_table_pop_due(scheduler_job_table_t *t, int64_t now_us,
                                   scheduler_job_fn *out_fn, void **out_ctx);

/* INT64_MAX if no job is scheduled -- the worker uses this to size its
 * sleep between ticks. */
int64_t scheduler_job_table_next_due(const scheduler_job_table_t *t);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SCHEDULER_JOBS_H */
