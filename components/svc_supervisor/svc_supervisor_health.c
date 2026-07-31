#include "svc_supervisor_health.h"

#include <string.h>

void svc_supervisor_health_init(svc_supervisor_health_t *g, int64_t now_us)
{
    memset(g, 0, sizeof(*g));
    g->start_us = now_us;
}

void svc_supervisor_health_report(svc_supervisor_health_t *g, health_condition_t cond, bool ok)
{
    if (ok) {
        g->conditions[cond] = true;
    }
}

health_gate_verdict_t svc_supervisor_health_evaluate(const svc_supervisor_health_t *g, int64_t now_us)
{
    bool all_ok = true;
    for (int i = 0; i < HEALTH_COND_COUNT; i++) {
        if (!g->conditions[i]) {
            all_ok = false;
            break;
        }
    }
    if (all_ok) {
        return HEALTH_GATE_PASS;
    }

    if (now_us - g->start_us >= HEALTH_GATE_WINDOW_US) {
        return HEALTH_GATE_TIMED_OUT;
    }
    return HEALTH_GATE_PENDING;
}
