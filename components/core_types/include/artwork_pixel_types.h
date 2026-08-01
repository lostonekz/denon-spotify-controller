/*
 * Pixel-buffer shape for decoded album artwork, and the safe copy-out
 * interface Phase 5's `ui` component uses to read it.
 *
 * Layer "core" (legal for `ui` to depend on) so the type can be shared
 * across the ui/svc boundary without `ui` including any svc_artwork
 * header. Phase 5 plan §1: `ui` never receives a raw pointer into
 * svc_artwork's pool -- lock_copy() copies into a caller-owned buffer
 * while svc_artwork's own cache_lock mutex is held, so the copy is
 * atomic with respect to a concurrent suspend freeing the backing
 * storage. svc_artwork's own ARTWORK_CACHE_SLOT_WIDTH/HEIGHT/BYTES
 * (artwork_cache.h) are aliases of these constants, not a second
 * definition -- one source of truth for the pixel shape.
 */
#ifndef CORE_ARTWORK_PIXEL_TYPES_H
#define CORE_ARTWORK_PIXEL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_types.h"   /* artwork_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

#define ARTWORK_PIXEL_WIDTH  320
#define ARTWORK_PIXEL_HEIGHT 320
#define ARTWORK_PIXEL_BYTES  (ARTWORK_PIXEL_WIDTH * ARTWORK_PIXEL_HEIGHT * 2u)   /* RGB565 */

/* out_cap_bytes lets a misused/mis-sized caller buffer fail cleanly (false)
 * instead of silently overflowing or under-filling. A real implementation
 * copies ARTWORK_PIXEL_BYTES into out_buf and returns true only if handle
 * is still valid and out_cap_bytes >= ARTWORK_PIXEL_BYTES; never blocks
 * behind a fetch or a JPEG decode -- see svc_artwork_pixel_lock_copy()'s
 * own contract (Phase 5 plan §1). */
typedef struct {
    void *ctx;
    bool (*lock_copy)(void *ctx, artwork_handle_t handle, uint16_t *out_buf, size_t out_cap_bytes);
} artwork_pixel_provider_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_ARTWORK_PIXEL_TYPES_H */
