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

/* Bulk forms of push/discard, for callers moving many elements at once.
 *
 * These exist because svc_logging pushes a whole formatted log line into a
 * byte ring (elem_size 1) on every single ESP_LOGx call in the system,
 * and doing that one core_pool_ring_push() call per byte meant ~256 calls
 * -- each with its own modulo, bounds check and 1-byte memcpy -- per line,
 * on whichever task happened to be logging. These turn the same work into
 * at most two real memcpy()s.
 *
 * Both stay policy-free, exactly as this header's own top comment
 * requires: push_bulk fills only what genuinely fits and reports how much
 * that was, and discard is a separate explicit call. A caller that wants
 * overwrite-oldest semantics composes the two itself and owns that
 * decision (svc_logging.c's ring_write_overwriting() is the worked
 * example) -- this module still decides nothing about what "full" means.
 *
 * push_bulk copies n * elem_size bytes from elems, in order, and returns
 * the number of elements actually pushed: n if they all fit, otherwise
 * however many did (0 when already full). Never overwrites unread data. */
size_t core_pool_ring_push_bulk(core_pool_ring_t *r, const void *elems, size_t n);

/* Drops the n oldest elements without copying them anywhere. Returns the
 * number actually dropped, which is less than n only if the ring held
 * fewer. */
size_t core_pool_ring_discard(core_pool_ring_t *r, size_t n);

size_t core_pool_ring_count(const core_pool_ring_t *r);
size_t core_pool_ring_capacity(const core_pool_ring_t *r);
size_t core_pool_ring_max_count(const core_pool_ring_t *r);
bool   core_pool_ring_is_full(const core_pool_ring_t *r);
bool   core_pool_ring_is_empty(const core_pool_ring_t *r);

#ifdef __cplusplus
}
#endif

#endif /* CORE_POOL_RING_H */
