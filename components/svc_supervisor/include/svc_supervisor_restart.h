/*
 * Pure restart/reboot attempt-cap math, over a caller-owned timestamp
 * history and an injected clock. docs/LIFECYCLE.md §1.3: "Reboot is capped
 * at 3 attempts in 10 minutes; beyond that the device holds the fault
 * screen and waits for a human." The same sliding-window mechanism serves
 * both that device-level reboot cap and each service's own max_restarts
 * (svc_supervisor.c parametrizes the window differently for each -- see
 * its comments); this module doesn't know or care which caller it's
 * counting for.
 *
 * No FreeRTOS, no ESP-IDF -- directly host-testable, same as core_fsm.
 */
#ifndef SVC_SUPERVISOR_RESTART_H
#define SVC_SUPERVISOR_RESTART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RESTART_HISTORY_CAPACITY 8

typedef struct {
    int64_t attempts[RESTART_HISTORY_CAPACITY];   /* oldest first */
    uint8_t count;
} restart_history_t;

typedef enum {
    RESTART_DECISION_ATTEMPT = 0,  /* under the cap -- go ahead and restart/reboot */
    RESTART_DECISION_DEGRADE,      /* at the cap, degraded_ok -- degrade instead */
    RESTART_DECISION_FAIL,         /* at the cap, not degradable -- fatal */
} restart_decision_t;

void restart_history_init(restart_history_t *h);

/* Expires entries older than `now_us - window_us`, then compares the
 * remaining count against max_attempts. Pass INT64_MAX as window_us for a
 * lifetime (never-expiring) cap rather than a sliding one.
 *
 * RESTART_DECISION_ATTEMPT: records now_us into `h` (the attempt itself
 * counts against the budget whether or not it goes on to succeed) and
 * returns ATTEMPT. Otherwise `h` is left as just-expired, unmodified by
 * this call -- a service that's already given up doesn't keep consuming
 * budget for a restart that never happened. */
restart_decision_t restart_history_evaluate(restart_history_t *h, int64_t now_us,
                                               int64_t window_us, uint8_t max_attempts,
                                               bool degraded_ok);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SUPERVISOR_RESTART_H */
