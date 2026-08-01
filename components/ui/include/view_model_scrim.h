/*
 * The Art-forward Now Playing scrim's adaptive opacity (Phase 5 plan §5) --
 * answers the design system's own flagged risk on the chosen layout:
 * "the scrim needs testing against pale covers." A fixed opacity tuned for
 * a typical (darker) cover lets white overlaid text wash out against a
 * pale one; this raises the scrim's bottom-stop opacity as the artwork's
 * own average luma rises.
 *
 * Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1. The luma SAMPLING itself
 * (averaging pixels over the bottom ~35% band of the real decoded artwork)
 * is a target-only concern (Tier 6, w_art_card) -- this module is only the
 * luma -> opacity mapping.
 */
#ifndef VIEW_MODEL_SCRIM_H
#define VIEW_MODEL_SCRIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Design system's own stated baseline: the scrim's bottom stop is 0.92
 * opacity for a typical cover. Expressed here as an 8-bit alpha (0..255,
 * matching LVGL's own lv_opa_t scale so the target-only wrapper that
 * consumes this is a direct assignment, no further conversion). */
#define VIEW_MODEL_SCRIM_BASELINE_OPA 235u   /* round(0.92 * 255) */
#define VIEW_MODEL_SCRIM_CEILING_OPA  250u   /* never fully opaque -- some
                                                * artwork always shows through,
                                                * even behind a pure-white cover */

/* avg_luma <= this stays at the baseline -- most covers are darker than
 * this and don't need any adjustment at all. */
#define VIEW_MODEL_SCRIM_LUMA_MIDPOINT 160u

/* Monotonic, non-decreasing in avg_luma (0..255): BASELINE_OPA for any
 * luma at or below the midpoint, ramping linearly to CEILING_OPA as luma
 * rises from the midpoint to 255. */
uint8_t view_model_scrim_opacity_for_luma(uint8_t avg_luma);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_SCRIM_H */
