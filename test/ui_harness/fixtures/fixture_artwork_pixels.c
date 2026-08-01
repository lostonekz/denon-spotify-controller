#include "fixture_artwork_pixels.h"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static void fill_diagonal_gradient(uint16_t *out, int r_lo, int g_lo, int b_lo,
                                    int r_hi, int g_hi, int b_hi)
{
    for (int32_t y = 0; y < ARTWORK_PIXEL_HEIGHT; y++) {
        for (int32_t x = 0; x < ARTWORK_PIXEL_WIDTH; x++) {
            int32_t t = (x + y) * 255 / (ARTWORK_PIXEL_WIDTH + ARTWORK_PIXEL_HEIGHT - 2);
            uint8_t r = (uint8_t)(r_lo + (r_hi - r_lo) * t / 255);
            uint8_t g = (uint8_t)(g_lo + (g_hi - g_lo) * t / 255);
            uint8_t b = (uint8_t)(b_lo + (b_hi - b_lo) * t / 255);
            out[y * ARTWORK_PIXEL_WIDTH + x] = rgb565(r, g, b);
        }
    }
}

void fixture_artwork_pixels_normal(uint16_t *out)
{
    fill_diagonal_gradient(out, 0x2A, 0x1E, 0x3C, 0x6E, 0x2E, 0x2A);   /* deep violet -> warm rust */
}

void fixture_artwork_pixels_pale(uint16_t *out)
{
    fill_diagonal_gradient(out, 0xF2, 0xEE, 0xE6, 0xFF, 0xFC, 0xF6);   /* near-white cover, plan §5 */
}
