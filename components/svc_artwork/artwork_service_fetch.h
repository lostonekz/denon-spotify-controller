/*
 * Private to svc_artwork -- deliberately NOT under include/, so it is not
 * part of this component's public surface and the include gate doesn't see
 * it. Split out of artwork_service.c for the 400-line limit; see
 * artwork_service_fetch.c's own header.
 */
#ifndef ARTWORK_SERVICE_FETCH_H
#define ARTWORK_SERVICE_FETCH_H

#include "artwork_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clears DOMAIN_ARTWORK's handle. Callers outside the fetch path use it for
 * the ordering artwork_cache.h requires: publish FAILED *before* freeing the
 * cache's backing memory, so a stale handle can never lock() against memory
 * that is already gone. */
void artwork_service_publish_failed(svc_artwork_t *a);

/* One fetch/decode/publish pass for whatever DOMAIN_PLAYBACK currently says.
 * Artwork-task-only. No-ops while suspended -- see artwork_service.h's
 * suspend contract for the crash that guard exists to prevent. */
void artwork_service_handle_track_change(svc_artwork_t *a);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_SERVICE_FETCH_H */
