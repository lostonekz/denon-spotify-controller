#include "fake_artwork_pixel_provider.h"

#include <string.h>

static bool fake_lock_copy(void *ctx, artwork_handle_t handle, uint16_t *out_buf, size_t out_cap_bytes)
{
    (void)handle;   /* the real provider validates the handle against its live cache;
                       this fixture has no cache to validate against, so any handle
                       is accepted as long as a pattern has been set */
    fake_artwork_pixel_provider_t *fake = (fake_artwork_pixel_provider_t *)ctx;
    if (fake->pattern == NULL || out_cap_bytes < ARTWORK_PIXEL_BYTES) {
        return false;
    }
    memcpy(out_buf, fake->pattern, ARTWORK_PIXEL_BYTES);
    return true;
}

void fake_artwork_pixel_provider_set_pattern(fake_artwork_pixel_provider_t *fake, const uint16_t *pattern)
{
    fake->pattern = pattern;
}

artwork_pixel_provider_t fake_artwork_pixel_provider_get(fake_artwork_pixel_provider_t *fake)
{
    artwork_pixel_provider_t p = { .ctx = fake, .lock_copy = fake_lock_copy };
    return p;
}
