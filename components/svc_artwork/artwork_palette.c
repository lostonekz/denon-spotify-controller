#include "artwork_palette.h"

#include <math.h>

#define ARTWORK_PALETTE_HUE_BINS 24

/* Judgement calls, not derived from any spec -- discard pixels that carry
 * little real colour information, then re-emit the modal hue vivid but
 * clamped away from the extremes so overlaid text stays legible. */
#define ARTWORK_PALETTE_MIN_VALUE          0.10f   /* discard near-black */
#define ARTWORK_PALETTE_MIN_SATURATION     0.15f   /* discard near-gray/white */
#define ARTWORK_PALETTE_BOOSTED_SATURATION 0.65f
#define ARTWORK_PALETTE_MIN_LUMINANCE      0.35f
#define ARTWORK_PALETTE_MAX_LUMINANCE      0.70f

static void rgb565_to_rgb888(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((px >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((px >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(px & 0x1Fu);
    *r = (uint8_t)(((uint32_t)r5 * 255u) / 31u);
    *g = (uint8_t)(((uint32_t)g6 * 255u) / 63u);
    *b = (uint8_t)(((uint32_t)b5 * 255u) / 31u);
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = (uint16_t)(((uint32_t)r * 31u + 127u) / 255u);
    uint16_t g6 = (uint16_t)(((uint32_t)g * 63u + 127u) / 255u);
    uint16_t b5 = (uint16_t)(((uint32_t)b * 31u + 127u) / 255u);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *h, float *s, float *v)
{
    float rf = (float)r / 255.0f;
    float gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f;

    float max = rf;
    if (gf > max) max = gf;
    if (bf > max) max = bf;
    float min = rf;
    if (gf < min) min = gf;
    if (bf < min) min = bf;
    float delta = max - min;

    *v = max;
    *s = (max <= 0.0f) ? 0.0f : (delta / max);

    if (delta <= 0.00001f) {
        *h = 0.0f;
    } else if (max == rf) {
        *h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
    } else if (max == gf) {
        *h = 60.0f * (((bf - rf) / delta) + 2.0f);
    } else {
        *h = 60.0f * (((rf - gf) / delta) + 4.0f);
    }
    if (*h < 0.0f) {
        *h += 360.0f;
    }
}

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;

    if (h < 60.0f)       { rf = c; gf = x; bf = 0.0f; }
    else if (h < 120.0f) { rf = x; gf = c; bf = 0.0f; }
    else if (h < 180.0f) { rf = 0.0f; gf = c; bf = x; }
    else if (h < 240.0f) { rf = 0.0f; gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; gf = 0.0f; bf = c; }
    else                 { rf = c; gf = 0.0f; bf = x; }

    *r = (uint8_t)((rf + m) * 255.0f);
    *g = (uint8_t)((gf + m) * 255.0f);
    *b = (uint8_t)((bf + m) * 255.0f);
}

uint16_t artwork_palette_dominant_color_rgb565(const artwork_pixel_buffer_t *buf)
{
    if (buf == NULL || buf->pixels == NULL || buf->width == 0 || buf->height == 0) {
        return 0;
    }

    uint32_t bin_count[ARTWORK_PALETTE_HUE_BINS]   = { 0 };
    float    bin_hue_sum[ARTWORK_PALETTE_HUE_BINS] = { 0 };

    size_t total_pixels = (size_t)buf->width * (size_t)buf->height;
    for (size_t i = 0; i < total_pixels; i++) {
        uint8_t r, g, b;
        rgb565_to_rgb888(buf->pixels[i], &r, &g, &b);

        float h, s, v;
        rgb_to_hsv(r, g, b, &h, &s, &v);

        if (v < ARTWORK_PALETTE_MIN_VALUE || s < ARTWORK_PALETTE_MIN_SATURATION) {
            continue;   /* near-black, near-white, or near-gray -- no real colour info */
        }

        int bin = (int)(h / (360.0f / (float)ARTWORK_PALETTE_HUE_BINS));
        if (bin >= ARTWORK_PALETTE_HUE_BINS) bin = ARTWORK_PALETTE_HUE_BINS - 1;
        if (bin < 0) bin = 0;

        bin_count[bin]++;
        bin_hue_sum[bin] += h;
    }

    int      best_bin   = -1;
    uint32_t best_count = 0;
    for (int i = 0; i < ARTWORK_PALETTE_HUE_BINS; i++) {
        if (bin_count[i] > best_count) {
            best_count = bin_count[i];
            best_bin   = i;
        }
    }

    if (best_bin < 0) {
        return rgb888_to_rgb565(128, 128, 128);   /* genuinely monochrome image -- neutral fallback */
    }

    float avg_hue = bin_hue_sum[best_bin] / (float)bin_count[best_bin];

    uint8_t out_r, out_g, out_b;
    hsv_to_rgb(avg_hue, ARTWORK_PALETTE_BOOSTED_SATURATION,
                (ARTWORK_PALETTE_MIN_LUMINANCE + ARTWORK_PALETTE_MAX_LUMINANCE) / 2.0f,
                &out_r, &out_g, &out_b);
    return rgb888_to_rgb565(out_r, out_g, out_b);
}
