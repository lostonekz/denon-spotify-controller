/*
 * Injected OS primitives, so core_* modules stay portable to a host test
 * with no FreeRTOS (docs/adr/0002-c-with-handle-based-di.md already
 * establishes this pattern for time_source_t -- "swappable for host
 * tests"; os_lock_if_t is the same idea applied to the other primitive
 * core_state/core_eventbus need). Real wiring gets a semaphore-backed lock
 * and an esp_timer-backed clock; host tests get a no-op lock (correct, not
 * a cheat -- Unity tests are single-threaded) and a controllable fake clock.
 *
 * Shared here rather than duplicated per-component because three or more
 * modules need each: core_eventbus and core_state need the lock;
 * core_state, svc_scheduler, and svc_supervisor's restart-policy math all
 * need the clock.
 */
#ifndef CORE_OS_IF_TYPES_H
#define CORE_OS_IF_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *ctx;
} os_lock_if_t;

typedef struct {
    int64_t (*now_us)(void *ctx);
    void    *ctx;
} time_source_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_OS_IF_TYPES_H */
