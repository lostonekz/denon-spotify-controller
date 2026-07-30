#include "svc_scheduler_jobs.h"

#include <stddef.h>
#include <stdint.h>

static scheduler_job_t *lookup(scheduler_job_table_t *t, scheduler_job_h h)
{
    if (h.index >= t->capacity) {
        return NULL;
    }
    scheduler_job_t *j = &t->jobs[h.index];
    if (!j->in_use || j->generation != h.generation) {
        return NULL;
    }
    return j;
}

void scheduler_job_table_init(scheduler_job_table_t *t, scheduler_job_t *storage, uint32_t capacity)
{
    t->jobs     = storage;
    t->capacity = capacity;

    for (uint32_t i = 0; i < capacity; i++) {
        storage[i].in_use     = false;
        storage[i].generation = 0;
    }
}

bool scheduler_job_table_post(scheduler_job_table_t *t, int64_t now_us, int64_t delay_us,
                                int64_t period_us, scheduler_job_fn fn, void *ctx,
                                scheduler_job_h *out)
{
    for (uint32_t i = 0; i < t->capacity; i++) {
        scheduler_job_t *j = &t->jobs[i];
        if (j->in_use) {
            continue;
        }

        j->in_use = true;
        j->generation++;
        if (j->generation == 0) {
            j->generation = 1;   /* 0 is reserved invalid */
        }
        j->due_us    = now_us + delay_us;
        j->period_us = period_us;
        j->fn        = fn;
        j->ctx       = ctx;

        out->index      = i;
        out->generation = j->generation;
        return true;
    }
    return false;
}

void scheduler_job_table_cancel(scheduler_job_table_t *t, scheduler_job_h h)
{
    scheduler_job_t *j = lookup(t, h);
    if (j == NULL) {
        return;
    }
    j->in_use = false;
}

bool scheduler_job_table_pop_due(scheduler_job_table_t *t, int64_t now_us,
                                   scheduler_job_fn *out_fn, void **out_ctx)
{
    int64_t  earliest_due = 0;
    int32_t  earliest_idx = -1;

    for (uint32_t i = 0; i < t->capacity; i++) {
        scheduler_job_t *j = &t->jobs[i];
        if (!j->in_use || j->due_us > now_us) {
            continue;
        }
        if (earliest_idx < 0 || j->due_us < earliest_due) {
            earliest_idx = (int32_t)i;
            earliest_due = j->due_us;
        }
    }

    if (earliest_idx < 0) {
        return false;
    }

    scheduler_job_t *j = &t->jobs[earliest_idx];
    *out_fn  = j->fn;
    *out_ctx = j->ctx;

    if (j->period_us > 0) {
        j->due_us += j->period_us;
        if (j->due_us <= now_us) {
            j->due_us = now_us + j->period_us;
        }
    } else {
        j->in_use = false;
    }

    return true;
}

int64_t scheduler_job_table_next_due(const scheduler_job_table_t *t)
{
    int64_t earliest = INT64_MAX;
    for (uint32_t i = 0; i < t->capacity; i++) {
        if (t->jobs[i].in_use && t->jobs[i].due_us < earliest) {
            earliest = t->jobs[i].due_us;
        }
    }
    return earliest;
}
