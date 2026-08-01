/*
 * Generation-tagged slot pool. docs/EVENTS_AND_STATE.md §5.2.
 *
 * The general mechanism behind artwork_handle_t: large payloads (album art)
 * are never copied into an event, only referenced by a {slot, generation}
 * handle. If the slot has since been evicted and reused for something else,
 * lock() sees the generation mismatch and returns NULL instead of handing
 * back someone else's data -- use-after-evict becomes a returned error, not
 * a corrupted frame. Same caller-owned-storage, no-malloc discipline as
 * core_pool_block.
 *
 * Generation bumps on ACQUIRE, not on release: a handle to a slot that has
 * been released but not yet reacquired still locks successfully (the data
 * is stale but not yet someone else's), matching what EVENTS_AND_STATE.md
 * actually describes as the failure case -- reuse, not mere release.
 */
#ifndef CORE_POOL_SLOT_H
#define CORE_POOL_SLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t slot;
    uint32_t generation;
} core_pool_handle_t;

typedef struct {
    uint8_t  *storage;        /* block_size * count bytes, caller-owned */
    uint32_t *generation;     /* caller-owned, count entries, init to 0 */
    int32_t  *free_list;      /* caller-owned, count entries */
    size_t    block_size;
    size_t    count;
    size_t    free_top;
    size_t    min_free;
    uint32_t  exhausted_count;
} core_pool_slot_t;

void core_pool_slot_init(core_pool_slot_t *p, uint8_t *storage, uint32_t *generation,
                         int32_t *free_list, size_t block_size, size_t count);

/* Acquires a free slot and bumps its generation. Returns false (out
 * untouched) if exhausted -- never blocks. */
bool core_pool_slot_acquire(core_pool_slot_t *p, core_pool_handle_t *out);

/* Returns the slot to the free list. Idempotent-unsafe by design: releasing
 * a handle twice corrupts the free list, same contract as free(). */
void core_pool_slot_release(core_pool_slot_t *p, core_pool_handle_t h);

/* Returns a pointer to the slot's storage if h is currently valid (slot in
 * range, generation matches, AND storage is non-NULL), else NULL. */
void *core_pool_slot_lock(const core_pool_slot_t *p, core_pool_handle_t h);

/* Nulls p->storage so every subsequent lock() fails closed, regardless of
 * generation. Deliberately leaves generation[]/free_list[] untouched --
 * those are separate caller-owned arrays that may still be valid to use
 * once storage is freed (e.g. artwork_cache_t's fixed-size arrays survive
 * a suspend/resume cycle; only the backing pixel buffer is reallocated).
 * Call after freeing the backing storage, before it's actually freed if
 * order matters to the caller -- this function does no freeing itself. */
void core_pool_slot_deinit(core_pool_slot_t *p);

size_t   core_pool_slot_free_count(const core_pool_slot_t *p);
size_t   core_pool_slot_min_free(const core_pool_slot_t *p);
uint32_t core_pool_slot_exhausted_count(const core_pool_slot_t *p);

#ifdef __cplusplus
}
#endif

#endif /* CORE_POOL_SLOT_H */
