/*
 * The OTA health gate. docs/PLATFORM.md §2.2: "A new image boots into
 * ESP_OTA_IMG_PENDING_VERIFY. It is confirmed only after it proves
 * itself... All gates green within 120 s -> mark_app_valid_cancel_rollback.
 * Otherwise -> deliberate reboot."
 *
 * docs/LIFECYCLE.md §1.4: svc_supervisor owns this verdict ("keeps svc_ota
 * from reaching into subsystems it should not know about"). Pure --
 * host-tested exactly like svc_supervisor_restart.c -- so svc_supervisor.c
 * (the glue that feeds it from TOPIC_NET/TOPIC_TIME) and app_wiring (which
 * relays the verdict to svc_ota, since svc has no self-edge) stay simple.
 *
 * Phase 2 tracks a subset of PLATFORM.md §2.2's 9 conditions:
 * "tasks feeding the watchdog" and "no watchdog trip" are merged into one
 * signal (too few real tasks exist yet to distinguish them meaningfully);
 * "panel initialised, UI rendered, touch enumerated" is hardcoded true (no
 * hal_display/ui until Phase 5) -- both documented gaps, not silent cuts.
 */
#ifndef SVC_SUPERVISOR_HEALTH_H
#define SVC_SUPERVISOR_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HEALTH_COND_BOOT_COMPLETE = 0,
    HEALTH_COND_TASKS_OK,           /* merged "feeding WDT" + "no trip" */
    HEALTH_COND_PANEL_UI,           /* Phase 2 stand-in: hardcoded true */
    HEALTH_COND_STORAGE_MOUNTED,
    HEALTH_COND_WIFI_IP,
    HEALTH_COND_SNTP_SYNCED,
    HEALTH_COND_TLS_HANDSHAKE,
    HEALTH_COND_HEAP_WATERMARK,
    HEALTH_COND_COUNT,
} health_condition_t;

typedef enum {
    HEALTH_GATE_PENDING = 0,
    HEALTH_GATE_PASS,
    HEALTH_GATE_TIMED_OUT,
} health_gate_verdict_t;

#define HEALTH_GATE_WINDOW_US ((int64_t)120 * 1000000)

typedef struct {
    bool     conditions[HEALTH_COND_COUNT];
    int64_t  start_us;
} svc_supervisor_health_t;

void svc_supervisor_health_init(svc_supervisor_health_t *g, int64_t now_us);

/* Sticky: a condition once reported true stays true for the rest of this
 * gate's life, even if reported false afterward -- the gate proves the
 * platform came up correctly THIS boot, not an ongoing liveness check. */
void svc_supervisor_health_report(svc_supervisor_health_t *g, health_condition_t cond, bool ok);

/* PASS the instant every condition is true, regardless of elapsed time.
 * Otherwise PENDING until HEALTH_GATE_WINDOW_US has elapsed since init(),
 * then TIMED_OUT. A pure computation from current state -- callers decide
 * when to stop polling once they see a non-PENDING verdict. */
health_gate_verdict_t svc_supervisor_health_evaluate(const svc_supervisor_health_t *g, int64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SUPERVISOR_HEALTH_H */
