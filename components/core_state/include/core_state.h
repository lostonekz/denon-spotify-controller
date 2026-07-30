/*
 * The single app_state_t snapshot store. docs/EVENTS_AND_STATE.md §6.
 *
 * Each of the 7 domains has exactly one writer; readers get a mutex-copied
 * snapshot (§6.1) so they can never observe a half-updated struct or hold a
 * pointer into shared memory. The lock is held only across the memcpy and
 * the three meta fields below -- no I/O, no allocation, no callbacks under
 * it, same constraint as core_eventbus's release hook.
 *
 * One store, one app_state_t, embedded directly (not caller-owned storage
 * like core_pool/core_eventbus) -- there is exactly one of these for the
 * life of the device, sized and known at compile time.
 *
 * read()/write() are generic over all 7 domains rather than one function
 * per domain, matching EVENTS_AND_STATE.md §6.1's own
 * `state_store_read(h, DOMAIN_PLAYBACK, &out)` example. Since the 7 domain
 * structs are different C types, out_size/in_size double as a runtime type
 * check: a mismatch (wrong domain/type pairing) is rejected rather than
 * silently truncated or overrun.
 */
#ifndef CORE_STATE_H
#define CORE_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "app_state_types.h"
#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    app_state_t    state;
    os_lock_if_t   lock;
    time_source_t  clock;
} core_state_t;

void core_state_init(core_state_t *s, os_lock_if_t lock, time_source_t clock);

/* False (out untouched) if out_size does not match domain's struct size. */
bool core_state_read(core_state_t *s, domain_id_t domain, void *out, size_t out_size);

/* Copies in_size bytes from `in` over the domain's struct (its meta field
 * included, but immediately overwritten below), then sets:
 *   meta.status     = status        (the writer's honest assessment)
 *   meta.updated_us = injected clock (the store's job, not the caller's --
 *                                      every write is comparably timestamped
 *                                      regardless of which service wrote it)
 *   meta.version++
 * False (domain unchanged) if in_size does not match the domain's struct
 * size. */
bool core_state_write(core_state_t *s, domain_id_t domain, link_status_t status,
                       const void *in, size_t in_size);

#ifdef __cplusplus
}
#endif

#endif /* CORE_STATE_H */
