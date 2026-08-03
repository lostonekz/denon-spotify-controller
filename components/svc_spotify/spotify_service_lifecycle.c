/*
 * svc_spotify's lifecycle surface: teardown, restart, the request to drop
 * the persistent API session, and the service descriptor. Target-only.
 *
 * Split out of spotify_service_task.c for this project's 400-line file
 * limit, along the seam that file's own top comment already drew between
 * "what the task does each iteration" and "how the service starts, stops
 * and is described to svc_supervisor".
 */
#include "svc_spotify_internal.h"

#include "esp_heap_caps.h"

void svc_spotify_teardown(void *handle)
{
    svc_spotify_t *s = (svc_spotify_t *)handle;

    if (s->task_handle != NULL) {
        s->task_should_stop = true;
        xSemaphoreTake(s->stopped_sem, portMAX_DELAY);
        s->task_handle = NULL;
    }
    if (s->ctrl_queue != NULL) {
        vQueueDelete(s->ctrl_queue);
        s->ctrl_queue = NULL;
    }
    if (s->playback_intent_queue != NULL) {
        vQueueDelete(s->playback_intent_queue);
        s->playback_intent_queue = NULL;
    }
    if (s->session_lock != NULL) {
        vSemaphoreDelete(s->session_lock);
        s->session_lock = NULL;
    }
    if (s->stopped_sem != NULL) {
        vSemaphoreDelete(s->stopped_sem);
        s->stopped_sem = NULL;
    }
    /* Freed only after the task is confirmed stopped above -- the task is
     * this buffer's only user, so there can be no in-flight request still
     * reading it by the time this runs. */
    if (s->resp_buf != NULL) {
        heap_caps_free(s->resp_buf);
        s->resp_buf = NULL;
    }

    spotify_service_state_reset(&s->state);
}

bool svc_spotify_restart(void *restart_ctx, void **out_handle)
{
    svc_spotify_restart_ctx_t *rc = (svc_spotify_restart_ctx_t *)restart_ctx;

    svc_spotify_teardown(rc->s);
    if (!svc_spotify_create(rc->s, &rc->cfg)) {
        return false;
    }
    *out_handle = rc->s;
    return true;
}

service_desc_t svc_spotify_service_desc(svc_spotify_restart_ctx_t *restart_ctx)
{
    /* LIFECYCLE.md §1.2's own worked example -- svc_spotify verbatim:
     * RESTART_BACKOFF_THEN_DEGRADE/max_restarts=3/degraded_ok=true. */
    service_desc_t desc = {
        .id           = SVC_ID_SPOTIFY,
        .name         = "svc_spotify",
        .requires     = SVC_BIT(SVC_ID_STORAGE) | SVC_BIT(SVC_ID_NET_WIFI) | SVC_BIT(SVC_ID_NET_TIME),
        .restart      = RESTART_BACKOFF_THEN_DEGRADE,
        .max_restarts = 3,
        .degraded_ok  = true,
        .teardown     = svc_spotify_teardown,
        .restart_fn   = svc_spotify_restart,
        .restart_ctx  = restart_ctx,
    };
    return desc;
}
