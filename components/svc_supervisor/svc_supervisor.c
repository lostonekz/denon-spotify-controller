#include "svc_supervisor.h"

#include "svc_supervisor_graph.h"

static void sup_lock(svc_supervisor_t *sup)   { sup->lock.lock(sup->lock.ctx); }
static void sup_unlock(svc_supervisor_t *sup) { sup->lock.unlock(sup->lock.ctx); }

static int find_index(const svc_supervisor_t *sup, service_id_t id)
{
    for (size_t i = 0; i < sup->count; i++) {
        if (sup->descs[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

void svc_supervisor_init(svc_supervisor_t *sup, const service_desc_t *descs, svc_state_t *states,
                           size_t count, os_lock_if_t lock, time_source_t clock,
                           void (*reboot_fn)(void))
{
    sup->descs     = descs;
    sup->states    = states;
    sup->count     = count;
    sup->lock       = lock;
    sup->clock      = clock;
    sup->reboot_fn = reboot_fn;

    restart_history_init(&sup->reboot_history);
    for (size_t i = 0; i < count; i++) {
        states[i].status = SUBSYS_INIT;
        states[i].handle = NULL;
        restart_history_init(&states[i].restart_history);
    }
}

bool svc_supervisor_boot(svc_supervisor_t *sup)
{
    size_t order[SVC_ID_COUNT];
    graph_result_t r = svc_supervisor_graph_order(sup->descs, sup->count, order);
    if (r != GRAPH_OK) {
        return false;
    }

    sup_lock(sup);
    for (size_t i = 0; i < sup->count; i++) {
        sup->states[order[i]].status = SUBSYS_READY;
    }
    sup_unlock(sup);
    return true;
}

bool svc_supervisor_get_status(const svc_supervisor_t *sup, service_id_t id,
                                 subsystem_status_t *out)
{
    int idx = find_index(sup, id);
    if (idx < 0) {
        return false;
    }
    *out = sup->states[idx].status;
    return true;
}

static void handle_reboot_policy(svc_supervisor_t *sup)
{
    int64_t now = sup->clock.now_us(sup->clock.ctx);

    sup_lock(sup);
    restart_decision_t d = restart_history_evaluate(&sup->reboot_history, now,
                                                       SVC_SUPERVISOR_REBOOT_WINDOW_US,
                                                       SVC_SUPERVISOR_REBOOT_MAX_ATTEMPTS,
                                                       false /* a reboot has no degrade path */);
    sup_unlock(sup);

    if (d == RESTART_DECISION_ATTEMPT) {
        sup->reboot_fn();
    }
    /* Otherwise: past the reboot cap -- hold. Phase 1 has no fault screen
     * (no UI yet); status stays FAILED for diagnostics/UART to observe. */
}

static void handle_service_restart_policy(svc_supervisor_t *sup, int idx)
{
    const service_desc_t *desc = &sup->descs[idx];
    int64_t now = sup->clock.now_us(sup->clock.ctx);

    sup_lock(sup);
    restart_decision_t d = restart_history_evaluate(&sup->states[idx].restart_history, now,
                                                       SVC_SUPERVISOR_LIFETIME_WINDOW_US,
                                                       desc->max_restarts, desc->degraded_ok);
    sup_unlock(sup);

    switch (d) {
    case RESTART_DECISION_ATTEMPT: {
        if (desc->teardown != NULL) {
            desc->teardown(sup->states[idx].handle);
        }
        void *new_handle = NULL;
        bool ok = (desc->restart_fn != NULL) && desc->restart_fn(desc->restart_ctx, &new_handle);

        sup_lock(sup);
        sup->states[idx].handle = ok ? new_handle : NULL;
        sup->states[idx].status = ok ? SUBSYS_READY : SUBSYS_FAILED;
        sup_unlock(sup);
        break;
    }
    case RESTART_DECISION_DEGRADE:
        sup_lock(sup);
        sup->states[idx].status = SUBSYS_DEGRADED;
        sup_unlock(sup);
        break;
    case RESTART_DECISION_FAIL:
        sup_lock(sup);
        sup->states[idx].status = SUBSYS_FAILED;
        sup_unlock(sup);
        break;
    }
}

void svc_supervisor_report_state(svc_supervisor_t *sup, service_id_t id, subsystem_status_t status)
{
    int idx = find_index(sup, id);
    if (idx < 0) {
        return;
    }

    sup_lock(sup);
    sup->states[idx].status = status;
    sup_unlock(sup);

    if (status != SUBSYS_FAILED) {
        return;
    }

    if (sup->descs[idx].restart == RESTART_REBOOT) {
        handle_reboot_policy(sup);
    } else {
        handle_service_restart_policy(sup, idx);
    }
}
