/*
 * Dominant-colour extraction from a decoded RGB565 artwork buffer.
 * docs/UI_ARCHITECTURE.md §5.2: build an HSV histogram over hue, discarding
 * near-black/near-white/low-saturation pixels (they carry no real colour
 * information), take the modal hue, then re-emit it with boosted
 * saturation and luminance clamped into a legible middle band -- vivid but
 * neither near-black nor near-white, so overlaid text stays readable
 * regardless of theme. Pure math, no allocation -- host-tested with
 * synthetic pixel data.
 */
#ifndef ARTWORK_PALETTE_H
#define ARTWORK_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint16_t *pixels;   /* RGB565, row-major, width*height entries */
    uint16_t        width;
    uint16_t        height;
} artwork_pixel_buffer_t;

/* Returns the dominant colour as RGB565, or mid-gray (0x8410) if every
 * pixel was filtered out (a genuinely monochrome/black/white image -- a
 * neutral fallback rather than an arbitrary hue with no real support), or
 * black (0) if buf is NULL/empty. */
uint16_t artwork_palette_dominant_color_rgb565(const artwork_pixel_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_PALETTE_H */
