/*
 * Synthesizes canned ARTWORK_PIXEL_WIDTH x ARTWORK_PIXEL_HEIGHT RGB565
 * buffers into caller-owned storage -- Phase 5 plan §5's "pale-cover pixel
 * buffer" fixture, and a normal mid-tone cover to compare it against. A
 * diagonal gradient, not a flat fill, so screen/widget code can visually
 * confirm orientation and scaling once Tier 6 actually renders these.
 */
#ifndef FIXTURE_ARTWORK_PIXELS_H
#define FIXTURE_ARTWORK_PIXELS_H

#include "artwork_pixel_types.h"

void fixture_artwork_pixels_normal(uint16_t *out);   /* mid-tone cover */
void fixture_artwork_pixels_pale(uint16_t *out);      /* near-white -- the scrim-contrast
                                                          * case named in plan §5 */

#endif /* FIXTURE_ARTWORK_PIXELS_H */
