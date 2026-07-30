#include "svc_scheduler.h"

#include <string.h>

#include "esp_log.h"

#define SVC_SCHEDULER_TASK_STACK   3072
#define SVC_SCHEDULER_TASK_PRIO    2
#define SVC_SCHEDULER_MAX_SLEEP_MS 1000   /* ceiling for the idle case (no job scheduled) */

static const char *TAG = "svc_scheduler";

static void worker_task(void *arg)
{
    svc_scheduler_t *s = (svc_scheduler_t *)arg;

    for (;;) {
        xSemaphoreTake(s->lock_sem, portMAX_DELAY);
        int64_t now = s->clock.now_us(s->clock.ctx);

        scheduler_job_fn fn;
        void *ctx;
        while (scheduler_job_table_pop_due(&s->table, now, &fn, &ctx)) {
            xSemaphoreGive(s->lock_sem);
            fn(ctx);
            xSemaphoreTake(s->lock_sem, portMAX_DELAY);
            now = s->clock.now_us(s->clock.ctx);
        }

        int64_t next_due = scheduler_job_table_next_due(&s->table);
        xSemaphoreGive(s->lock_sem);

        int64_t sleep_ms = SVC_SCHEDULER_MAX_SLEEP_MS;
        if (next_due != INT64_MAX) {
            sleep_ms = (next_due - now) / 1000;
            if (sleep_ms < 0) {
                sleep_ms = 0;
            } else if (sleep_ms > SVC_SCHEDULER_MAX_SLEEP_MS) {
                sleep_ms = SVC_SCHEDULER_MAX_SLEEP_MS;
            }
        }

        /* Woken early by post()/cancel() giving this semaphore (a nearer
         * deadline may have just been added), or times out on its own. */
        xSemaphoreTake(s->wake_sem, pdMS_TO_TICKS(sleep_ms));
    }
}

bool svc_scheduler_create(svc_scheduler_t *s, time_source_t clock)
{
    memset(s, 0, sizeof(*s));
    scheduler_job_table_init(&s->table, s->job_storage, SVC_SCHEDULER_MAX_JOBS);
    s->clock = clock;

    s->lock_sem = xSemaphoreCreateMutex();
    if (s->lock_sem == NULL) {
        ESP_LOGE(TAG, "failed to create job table mutex");
        return false;
    }

    s->wake_sem = xSemaphoreCreateBinary();
    if (s->wake_sem == NULL) {
        ESP_LOGE(TAG, "failed to create wake semaphore");
        vSemaphoreDelete(s->lock_sem);
        s->lock_sem = NULL;
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(worker_task, "svc_scheduler", SVC_SCHEDULER_TASK_STACK,
                                             s, SVC_SCHEDULER_TASK_PRIO, &s->task_handle,
                                             tskNO_AFFINITY);
    if (ok != pdPASS || s->task_handle == NULL) {
        ESP_LOGE(TAG, "failed to create worker task");
        vSemaphoreDelete(s->lock_sem);
        vSemaphoreDelete(s->wake_sem);
        s->lock_sem    = NULL;
        s->wake_sem    = NULL;
        s->task_handle = NULL;
        return false;
    }

    return true;
}

bool svc_scheduler_post_once(svc_scheduler_t *s, int64_t delay_us,
                               scheduler_job_fn fn, void *ctx, scheduler_job_h *out)
{
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    int64_t now = s->clock.now_us(s->clock.ctx);
    bool ok = scheduler_job_table_post(&s->table, now, delay_us, 0, fn, ctx, out);
    xSemaphoreGive(s->lock_sem);

    if (ok) {
        xSemaphoreGive(s->wake_sem);
    }
    return ok;
}

bool svc_scheduler_post_periodic(svc_scheduler_t *s, int64_t period_us,
                                   scheduler_job_fn fn, void *ctx, scheduler_job_h *out)
{
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    int64_t now = s->clock.now_us(s->clock.ctx);
    bool ok = scheduler_job_table_post(&s->table, now, period_us, period_us, fn, ctx, out);
    xSemaphoreGive(s->lock_sem);

    if (ok) {
        xSemaphoreGive(s->wake_sem);
    }
    return ok;
}

void svc_scheduler_cancel(svc_scheduler_t *s, scheduler_job_h job)
{
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    scheduler_job_table_cancel(&s->table, job);
    xSemaphoreGive(s->lock_sem);
}
