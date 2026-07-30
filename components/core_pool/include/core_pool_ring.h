/*
 * SPSC ring buffer over caller-owned storage. docs/MODULES.md §2.
 *
 * Exactly one producer, one consumer -- by construction, not by convention,
 * so no lock is needed here (unlike core_pool_block/core_pool_slot, which
 * are shared across arbitrary callers and need one at the call site).
 * Policy-free on purpose: what happens when the ring is full (replace the
 * oldest, drop the newest, who gets notified) is domain-specific and lives
 * one layer up, in whatever uses this (core_eventbus's per-topic overflow
 * policy, EVENTS_AND_STATE.md §5.3) -- core_pool owns none of that.
 */
#ifndef CORE_POOL_RING_H
#define CORE_POOL_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;     /* elem_size * capacity bytes, caller-owned */
    size_t   elem_size;
    size_t   capacity;
    size_t   head;        /* next write index */
    size_t   tail;        /* next read index */
    size_t   count;
    size_t   max_count;   /* high-water */
} core_pool_ring_t;

void core_pool_ring_init(core_pool_ring_t *r, uint8_t *storage,
                         size_t elem_size, size_t capacity);

/* Copies elem_size bytes from elem into the ring. False, ring unchanged, if
 * full -- the caller decides what "full" means (drop this, or evict the
 * oldest first via pop() then push() again). */
bool core_pool_ring_push(core_pool_ring_t *r, const void *elem);

/* Copies the oldest element's elem_size bytes into out. False, out
 * untouched, if empty. */
bool core_pool_ring_pop(core_pool_ring_t *r, void *out);

size_t core_pool_ring_count(const core_pool_ring_t *r);
size_t core_pool_ring_capacity(const core_pool_ring_t *r);
size_t core_pool_ring_max_count(const core_pool_ring_t *r);
bool   core_pool_ring_is_full(const core_pool_ring_t *r);
bool   core_pool_ring_is_empty(const core_pool_ring_t *r);

#ifdef __cplusplus
}
#endif

#endif /* CORE_POOL_RING_H */
