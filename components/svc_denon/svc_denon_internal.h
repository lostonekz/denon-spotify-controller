/*
 * Private cross-file declarations for svc_denon's target-only files
 * (denon_service.c / denon_service_link.c / denon_service_io.c /
 * denon_service_target.c). Not installed, not part of the public API in
 * include/ -- mirrors svc_ota's svc_ota_internal.h.
 */
#ifndef SVC_DENON_INTERNAL_H
#define SVC_DENON_INTERNAL_H

#include "denon_service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DENON_TASK_STACK 6144   /* ARCHITECTURE.md §4's table: "denon", core 0, prio 5, 6 KB */
#define DENON_TASK_PRIO  5
#define DENON_TASK_CORE  0

/* denon_service_target.c -- the "denon" task entry point. */
void denon_service_task(void *pv);

/* denon_service_link.c */
denon_link_fsm_actions_t denon_service_link_real_actions(void);
void                        denon_service_link_drive(svc_denon_t *d);

/* denon_service_io.c -- one pass: drain+send due cmdq entries, poll the
 * active transport for incoming lines, parse/publish/write state. Called
 * once per denon task iteration. */
bool denon_service_io_tick(svc_denon_t *d);

#ifdef __cplusplus
}
#endif

#endif /* SVC_DENON_INTERNAL_H */
