/*
 * OKLCH -> RGB565 conversion for the theme's design-system color tokens
 * (Phase 5 plan's Context: the user's own design system specifies every
 * color as an OKLCH triple, e.g. oklch(0.80 0.17 155) for the accent
 * green). Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 * lv_color_t construction from the RGB565 this produces is a thin
 * target-only wrapper (a single lv_color_make()-style call), not part of
 * this module.
 *
 * Pipeline: OKLCH -> Oklab -> linear sRGB -> gamma-encoded sRGB -> 5/6/5
 * quantization, using the standard published Oklab <-> linear-sRGB
 * matrices (Bjorn Ottosson). Out-of-gamut results (chroma too high for a
 * given lightness/hue to represent in sRGB) are clamped per-channel to
 * [0,1] before quantizing -- not a perceptually-exact gamut mapping, but
 * correct and stable for this project's own hand-picked, already-in-gamut
 * design tokens.
 */
#ifndef THEME_TOKENS_H
#define THEME_TOKENS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double l;       /* lightness, 0..1 */
    double c;       /* chroma, typically 0..~0.4 */
    double h_deg;   /* hue, degrees, 0..360 */
} oklch_color_t;

/* Packed RGB565: R in bits 15..11, G in bits 10..5, B in bits 4..0 --
 * the same representation artwork_state_t.dominant_color_rgb565 and
 * artwork_cache.c's pixel buffers already use throughout this codebase. */
uint16_t theme_tokens_oklch_to_rgb565(oklch_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* THEME_TOKENS_H */
