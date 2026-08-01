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
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "artwork_pixel_types.h"   /* ARTWORK_PIXEL_WIDTH/HEIGHT/BYTES -- one source of
                                      * truth for the pixel shape, shared with `ui`
                                      * (Phase 5 plan §1) without `ui` including this header */
#include "core_pool_slot.h"
#include "event_types.h"   /* artwork_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

#define ARTWORK_CACHE_SLOT_COUNT  3   /* MEMORY.md's budget; Phase 4 only ever holds 1 "live" */
#define ARTWORK_CACHE_SLOT_WIDTH  ARTWORK_PIXEL_WIDTH
#define ARTWORK_CACHE_SLOT_HEIGHT ARTWORK_PIXEL_HEIGHT
#define ARTWORK_CACHE_SLOT_BYTES  ARTWORK_PIXEL_BYTES

/* Phase 5 Tier -1: `lock` makes every pool touch (acquire/release/lock/
 * alloc/free/lock_copy) mutually exclusive across tasks -- svc_artwork's own
 * "artwork" task (handle_track_change's acquire/lock/release, and now
 * task-owned suspend/resume's alloc/free) and `ui`'s cross-task lock_copy()
 * calls. Created lazily on first alloc(), destroyed only by
 * artwork_cache_teardown() -- deleting a mutex a task might be blocked on is
 * unsafe, so it must outlive individual suspend/resume (free/alloc) cycles,
 * not be recreated by them. */
typedef struct {
    core_pool_slot_t   pool;
    uint8_t            *storage;   /* heap_caps_malloc'd (PSRAM); NULL while suspended */
    uint32_t            generation[ARTWORK_CACHE_SLOT_COUNT];
    int32_t             free_list[ARTWORK_CACHE_SLOT_COUNT];
    SemaphoreHandle_t   lock;
} artwork_cache_t;

/* Allocates the backing pixel storage and (re-)initializes the pool with
 * fresh generation counters. Called at svc_artwork_create() and at
 * resume(). False on allocation failure. */
bool artwork_cache_alloc(artwork_cache_t *c);

/* Frees the backing pixel storage (mutex-guarded, so a concurrent
 * lock()/lock_copy() sees either the fully-freed or fully-allocated state,
 * never a half-freed one). Idempotent -- safe to call when already freed.
 * Does NOT destroy the mutex -- see artwork_cache_teardown(). The CALLER
 * (artwork_service.c) must clear artwork_state_t.handle to {0,0} and
 * publish ARTWORK_EVT_FAILED BEFORE calling this, not after -- see this
 * file's own ownership contract above. */
void artwork_cache_free(artwork_cache_t *c);

/* Acquires a slot. False (out untouched) if the pool is exhausted or not
 * currently allocated (suspended). */
bool artwork_cache_acquire(artwork_cache_t *c, artwork_handle_t *out_handle);

/* Idempotent-unsafe by design, same contract as core_pool_slot_release() --
 * only svc_artwork's own "release the previous slot on the next fetch"
 * logic calls this, exactly once per acquired handle. */
void artwork_cache_release(artwork_cache_t *c, artwork_handle_t handle);

/* NULL if handle is stale (already released and reused) or the pool isn't
 * currently allocated. Same-task use only (e.g. artwork_service.c's own
 * handle_track_change(), immediately followed by a decode into the
 * returned pointer on that same task) -- safe there because suspend/resume
 * are task-owned too (Phase 5 plan §1), so nothing on that same task can
 * concurrently free the storage out from under the caller. A DIFFERENT
 * task must use artwork_cache_lock_copy() instead, never this function --
 * this one releases the mutex before returning the pointer, so it gives no
 * protection against a genuinely concurrent free(). */
uint16_t *artwork_cache_lock(artwork_cache_t *c, artwork_handle_t handle);

/* The cross-task-safe accessor: locks, copies ARTWORK_CACHE_SLOT_BYTES into
 * out_buf, and unlocks, all while holding the mutex -- so the copy is
 * atomic with respect to a concurrent free() on another task. False (out_buf
 * untouched) if handle is stale, the pool isn't allocated, or
 * out_cap_bytes is smaller than ARTWORK_CACHE_SLOT_BYTES. This is the
 * function backing artwork_pixel_provider_t.lock_copy (Phase 5 plan §1) --
 * never blocks behind a fetch or decode, only ever contends with another
 * short acquire/release/lock/alloc/free call. */
bool artwork_cache_lock_copy(artwork_cache_t *c, artwork_handle_t handle,
                               uint16_t *out_buf, size_t out_cap_bytes);

/* Final cleanup: frees storage if still allocated (artwork_cache_free(),
 * idempotent), then destroys the mutex. Only svc_artwork_teardown() calls
 * this -- suspend()/resume() cycles call artwork_cache_free()/alloc()
 * directly and must never destroy the mutex mid-service-lifetime. */
void artwork_cache_teardown(artwork_cache_t *c);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_CACHE_H */
