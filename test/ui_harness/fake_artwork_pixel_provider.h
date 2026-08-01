/*
 * Host fixture standing in for svc_artwork_pixel_provider() (Phase 5 plan
 * §1) -- test/ui_harness has no real svc_artwork, so lock_copy() here just
 * fills the caller's buffer from a canned in-memory pattern selected by the
 * active fixture (fixtures/fixtures.h) instead of copying out of a real
 * cache pool. Always succeeds once a pattern has been set; unlike the real
 * provider it does not model a concurrent-suspend race, since nothing in
 * this harness can suspend it.
 */
#ifndef FAKE_ARTWORK_PIXEL_PROVIDER_H
#define FAKE_ARTWORK_PIXEL_PROVIDER_H

#include "artwork_pixel_types.h"

typedef struct {
    const uint16_t *pattern;   /* ARTWORK_PIXEL_WIDTH*ARTWORK_PIXEL_HEIGHT RGB565, or NULL */
} fake_artwork_pixel_provider_t;

void fake_artwork_pixel_provider_set_pattern(fake_artwork_pixel_provider_t *fake, const uint16_t *pattern);
artwork_pixel_provider_t fake_artwork_pixel_provider_get(fake_artwork_pixel_provider_t *fake);

#endif /* FAKE_ARTWORK_PIXEL_PROVIDER_H */
