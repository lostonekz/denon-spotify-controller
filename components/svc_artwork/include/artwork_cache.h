/*
 * The generation-handled, single-owner slot pool backing
 * artwork_state_t.handle (not refcounted -- LIFECYCLE.md/MODULES.md's
 * wording is corrected in Tier 7), plus its own suspend/resume-driven
 * allocation (PLATFORM.md §2.4: "svc_artwork is suspended and its 600 KiB
 * pool released" during an OTA image download -- more than pausing
 * fetches, the backing memory itself is freed and reallocated).
 *
 * core_pool_slot_t (components/core_pool) is storage-agnostic by its own
 * contract; the pixel storage array lives here, target-only, allocated
 * from PSRAM (MALLOC_CAP_SPIRAM) since 3 x 200 KiB has no business in
 * internal SRAM.
 *
 * Handle ownership contract (this project's Phase 4 plan §8): only
 * svc_artwork ever calls artwork_cache_release() -- ARTWORK_EVT_READY's
 * handle is a notification copy, not an ownership grant. A consumer must
 * artwork_cache_lock() immediately before use, every time, and treat a
 * failed lock (stale generation) as "no artwork right now," never cache
 * the returned pointer across a yield. TOPIC_ARTWORK's on_release policy
 * is NULL (app_wiring, Tier 4) -- wiring up the bus's own drop-triggered
 * release alongside this file's release-on-supersede would double-release
 * the first time an event was ever dropped undelivered.
 */
#ifndef ARTWORK_CACHE_H
#define ARTWORK_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "core_pool_slot.h"
#include "event_types.h"   /* artwork_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

#define ARTWORK_CACHE_SLOT_COUNT  3   /* MEMORY.md's budget; Phase 4 only ever holds 1 "live" */
#define ARTWORK_CACHE_SLOT_WIDTH  320
#define ARTWORK_CACHE_SLOT_HEIGHT 320
#define ARTWORK_CACHE_SLOT_BYTES  (ARTWORK_CACHE_SLOT_WIDTH * ARTWORK_CACHE_SLOT_HEIGHT * 2u)   /* RGB565 */

typedef struct {
    core_pool_slot_t pool;
    uint8_t          *storage;   /* heap_caps_malloc'd (PSRAM); NULL while suspended */
    uint32_t          generation[ARTWORK_CACHE_SLOT_COUNT];
    int32_t           free_list[ARTWORK_CACHE_SLOT_COUNT];
} artwork_cache_t;

/* Allocates the backing pixel storage and (re-)initializes the pool with
 * fresh generation counters. Called at svc_artwork_create() and at
 * resume(). False on allocation failure. */
bool artwork_cache_alloc(artwork_cache_t *c);

/* Frees the backing pixel storage. Called at suspend() and teardown().
 * Idempotent -- safe to call when already freed. The CALLER (artwork_service.c)
 * must clear artwork_state_t.handle to {0,0} and publish ARTWORK_EVT_FAILED
 * BEFORE calling this, not after -- see this file's own ownership contract
 * above. */
void artwork_cache_free(artwork_cache_t *c);

/* Acquires a slot. False (out untouched) if the pool is exhausted or not
 * currently allocated (suspended). */
bool artwork_cache_acquire(artwork_cache_t *c, artwork_handle_t *out_handle);

/* Idempotent-unsafe by design, same contract as core_pool_slot_release() --
 * only svc_artwork's own "release the previous slot on the next fetch"
 * logic calls this, exactly once per acquired handle. */
void artwork_cache_release(artwork_cache_t *c, artwork_handle_t handle);

/* NULL if handle is stale (already released and reused) or the pool isn't
 * currently allocated. Lock immediately before use; never hold the
 * returned pointer across a yield. */
uint16_t *artwork_cache_lock(artwork_cache_t *c, artwork_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_CACHE_H */
