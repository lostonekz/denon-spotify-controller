/*
 * UI_ARCHITECTURE.md §5.1's semantic color tokens as real C values -- no
 * such header existed before Phase 5 Tier 6 (only the doc's own prose
 * table); every screen/widget built from here on refers to these, never a
 * raw hex literal, per §5.1's own rule ("no hex colour... appears outside
 * theme/").
 *
 * Values are the doc's current hex table as-is -- already-decided settings
 * this tier writes real code around, not re-investigates (same discipline
 * Tier 3 used for RGB timing). docs/ROADMAP.md Tier 8 reconciles this
 * whole file against the design system's real OKLCH values once (the doc's
 * own §5.1 heading already flags its hex table as provisional pending
 * that pass).
 */
#ifndef THEME_COLORS_H
#define THEME_COLORS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_COLOR_BG              lv_color_hex(0x0A0A0C)   /* near-black, slightly cool */
#define THEME_COLOR_SURFACE         lv_color_hex(0x141417)   /* cards */
#define THEME_COLOR_SURFACE_HIGH    lv_color_hex(0x1F1F24)   /* raised / pressed */
#define THEME_COLOR_TEXT_PRIMARY    lv_color_hex(0xF5F5F7)
#define THEME_COLOR_TEXT_SECONDARY  lv_color_hex(0xA0A0A8)
#define THEME_COLOR_TEXT_TERTIARY   lv_color_hex(0x6A6A72)

/* THEME_COLOR_ACCENT/ACCENT_MUTED are deliberately NOT here -- §5.1 marks
 * them "dynamic, from album art"; now_playing_vm_t.accent_rgb565 carries
 * the real per-track value, screens/widgets take it as a parameter.
 *
 * THEME_COLOR_STALE/DANGER have no numeric value in §5.1's own table
 * (named but not specified) -- w_status_pill needs a real, visually
 * distinct value for each (the doc's fallback text, "text drops to
 * secondary," isn't enough on its own for a pill that must read as a
 * distinct indicator, not just dimmed text). Chosen here, flagged for the
 * same Tier 8 reconciliation as the rest of this file. */
#define THEME_COLOR_STALE           lv_color_hex(0xC9A227)   /* muted amber -- old, not broken */
#define THEME_COLOR_DANGER          lv_color_hex(0xD9534F)   /* muted red -- offline / rejected */

#ifdef __cplusplus
}
#endif

#endif /* THEME_COLORS_H */
