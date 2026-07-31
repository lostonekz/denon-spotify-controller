/*
 * Private cross-file declarations for svc_spotify's target-only files
 * (spotify_service.c / spotify_service_session.c / spotify_service_task.c).
 * Not installed, not part of the public API in include/ -- mirrors
 * svc_denon's svc_denon_internal.h.
 */
#ifndef SVC_SPOTIFY_INTERNAL_H
#define SVC_SPOTIFY_INTERNAL_H

#include "spotify_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ARCHITECTURE.md §4's table originally specified 8 KB -- found on real
 * hardware: a stack overflow during the token exchange (mbedTLS's TLS
 * handshake is itself stack-hungry, on top of spotify_auth.c's own
 * code_encoded/redirect_encoded/body locals, sized generously to fit real
 * Spotify authorization codes -- see spotify_auth.c's own comment). Bumped
 * with real margin, not just enough to clear one observed crash; docs
 * updated to match (Tier 7 already reconciles docs against what was
 * actually built, not the other way around). */
#define SPOTIFY_TASK_STACK 12288
#define SPOTIFY_TASK_PRIO  5
#define SPOTIFY_TASK_CORE  0

/* spotify_service_session.c -- real session_fsm actions + the drive loop
 * that turns cross-boundary result flags into the real follow-up dispatch. */
spotify_session_fsm_actions_t spotify_service_session_real_actions(void);
void                            spotify_service_session_drive(svc_spotify_t *s);

/* One tick's worth of ctrl_queue draining + playback-intent draining +
 * poll scheduling (poll/force-poll/backoff). */
void spotify_service_tick(svc_spotify_t *s);

/* spotify_service_task.c -- task entry point. */
void spotify_service_task(void *pv);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SPOTIFY_INTERNAL_H */
