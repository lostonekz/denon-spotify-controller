/*
 * The "denon" task entry point, teardown, intent submission, restart, and
 * the service descriptor. Target-only -- see denon_service.h's top
 * comment.
 */
#include "svc_denon_internal.h"

#include "denon_intent.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Tier -1 fix: without this, the loop had no delay anywhere in its body.
 * denon_service_io_tick() returns immediately (no blocking at all) whenever
 * no transport is up (IDLE, DISCOVERING, CONNECTING states, BACKOFF states,
 * RELEASED), and HTTP fallback's own poll() also returns immediately
 * whenever its ~2s interval isn't yet due -- so the task spun at 100% CPU
 * on core 0 for the full backoff window, and continuously while operating
 * in HTTP fallback. A short bounded sleep whenever the tick did no real
 * blocking work closes both cases without needing a real event-driven
 * wake, which this bug doesn't warrant. */
#define DENON_TASK_IDLE_DELAY_MS 50u

void denon_service_task(void *pv)
{
    svc_denon_t *d = (svc_denon_t *)pv;
    while (!d->task_should_stop) {
        denon_service_link_drive(d);
        bool blocked = denon_service_io_tick(d);
        if (!blocked) {
            vTaskDelay(pdMS_TO_TICKS(DENON_TASK_IDLE_DELAY_MS));
        }
    }
    xSemaphoreGive(d->stopped_sem);
    vTaskDelete(NULL);
}

void svc_denon_teardown(void *handle)
{
    svc_denon_t *d = (svc_denon_t *)handle;

    if (d->task_handle != NULL) {
        d->task_should_stop = true;
        /* Bounded by denon_service_task's own loop: at most one
         * DENON_IO_POLL_TIMEOUT_MS (250ms) blocking call plus one
         * DENON_TASK_IDLE_DELAY_MS (50ms) idle sleep between checks
         * of task_should_stop. */
        xSemaphoreTake(d->stopped_sem, portMAX_DELAY);
        d->task_handle = NULL;
    }
    if (d->cmdq_lock != NULL) {
        vSemaphoreDelete(d->cmdq_lock);
        d->cmdq_lock = NULL;
    }
    if (d->stopped_sem != NULL) {
        vSemaphoreDelete(d->stopped_sem);
        d->stopped_sem = NULL;
    }

    /* Force-close regardless of link_fsm state -- release_link (the FSM's
     * own action) only fires on the SLEEP_ENTER path, not on teardown. */
    d->telnet_iface.close(d->telnet_iface.ctx);
    d->http_iface.close(d->http_iface.ctx);

    denon_service_state_reset(&d->state);
}

bool svc_denon_submit_intent(svc_denon_t *d, const intent_t *intent)
{
    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    denon_intent_result_t r = denon_intent_to_cmd(intent, d->state.volume_limit_half_db,
                                                     cmd, sizeof(cmd), &is_absolute, &cls);
    if (r != DENON_INTENT_RESULT_OK) {
        return false;
    }

    bool ok;
    xSemaphoreTake(d->cmdq_lock, portMAX_DELAY);
    if (is_absolute) {
        denon_cmdq_submit_absolute(&d->state.cmdq, cls, cmd);
        ok = true;
    } else {
        ok = denon_cmdq_submit_discrete(&d->state.cmdq, cmd);
    }
    xSemaphoreGive(d->cmdq_lock);
    return ok;
}

bool svc_denon_restart(void *restart_ctx, void **out_handle)
{
    svc_denon_restart_ctx_t *rc = (svc_denon_restart_ctx_t *)restart_ctx;

    svc_denon_teardown(rc->d);
    if (!svc_denon_create(rc->d, &rc->cfg)) {
        return false;
    }
    *out_handle = rc->d;
    return true;
}

service_desc_t svc_denon_service_desc(svc_denon_restart_ctx_t *restart_ctx)
{
    /* LIFECYCLE.md §1.2's own worked example for the restartable class
     * (svc_spotify: one socket/session + one queue, teardown provably
     * complete) uses RESTART_BACKOFF_THEN_DEGRADE/max_restarts=3 -- matched
     * here rather than RESTART_IMMEDIATE, which is reserved for a case
     * provably safe to retry unboundedly (a service-task crash is a bug,
     * distinct from denon_link_fsm's own internal connection backoff,
     * which already owns "AVR not present"). degraded_ok = true --
     * LIFECYCLE.md §1.1's own canonical normal-DEGRADED example is "no AVR
     * on the network." */
    service_desc_t desc = {
        .id           = SVC_ID_DENON,
        .name         = "svc_denon",
        .requires     = SVC_BIT(SVC_ID_STORAGE) | SVC_BIT(SVC_ID_NET_WIFI),
        .restart      = RESTART_BACKOFF_THEN_DEGRADE,
        .max_restarts = 3,
        .degraded_ok  = true,
        .teardown     = svc_denon_teardown,
        .restart_fn   = svc_denon_restart,
        .restart_ctx  = restart_ctx,
    };
    return desc;
}
