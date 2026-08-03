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

#define THEME_COLOR_BG              lv_color_hex(0x090C0A)   /* near-black, slightly cool --
                                                                  oklch(0.15 0.006 160), the
                                                                  design system's own frame-
                                                                  base-fill token, computed via
                                                                  theme_tokens_oklch_to_rgb565() */
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

/* w_stroke_label's own black outline -- keeps overlay text legible against
 * artwork of any brightness/color, independent of the scrim (which only
 * covers the bottom band of the card, not the top-corner labels).
 * Verified correct as-is against the design system's real OKLCH value
 * (oklch(0.08 0 0) quantizes to exactly pure black, 0x0000, at this
 * display's real RGB565 depth -- computed via theme_tokens_oklch_to_rgb565(),
 * not eyeballed) during the Art-forward redesign's Tier 8 token pass below. */
#define THEME_COLOR_TEXT_STROKE     lv_color_hex(0x000000)

/* Art-forward redesign (Phase 5 addendum) -- real OKLCH -> RGB565 values,
 * computed via theme_tokens_oklch_to_rgb565() (components/ui/theme/
 * theme_tokens.c), not hand-picked. This is the Tier 8 reconciliation this
 * file's own top comment has been flagging as pending since Tier 6. */
#define THEME_COLOR_TEXT_ONIMAGE    lv_color_hex(0xE4E4E4)   /* oklch(0.92 0 0) -- header/
                                                                  device-name text over full-
                                                                  bleed art. Deliberately
                                                                  distinct from
                                                                  THEME_COLOR_TEXT_PRIMARY
                                                                  (0xF5F5F7), ~17pt brighter --
                                                                  not interchangeable */
#define THEME_COLOR_ACCENT_LIVE     lv_color_hex(0x4ADD8C)   /* oklch(0.80 0.17 155) -- fixed
                                                                  volume-track fill. NOT the old
                                                                  per-track dynamic accent
                                                                  (now_playing_vm_t.accent_rgb565,
                                                                  removed by this redesign --
                                                                  the scrub bar's fill is flat
                                                                  white, not art-derived) */

#ifdef __cplusplus
}
#endif

#endif /* THEME_COLORS_H */
