/*
 * UI_ARCHITECTURE.md §5.4's metrics/motion tokens as real C values -- same
 * story as theme_colors.h, no such header existed before Phase 5 Tier 6.
 *
 * THEME_RADIUS_PILL is deliberately not here -- widgets that want a fully
 * round shape use LVGL's own LV_RADIUS_CIRCLE token directly (already the
 * idiom ui_router.c's page dots used since Tier 4); a THEME_ constant that
 * just aliases an LVGL constant adds a name without adding a real decision.
 *
 * Motion durations are milliseconds, ready for lv_anim_set_duration()
 * directly -- easing curves (ease-out/ease-in-out/linear, per §5.4's own
 * table) are each animation's own lv_anim_set_path_cb() call, not encoded
 * here; a numeric token can't carry a function pointer.
 */
#ifndef THEME_METRICS_H
#define THEME_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_RADIUS_CARD   20
#define THEME_RADIUS_SHEET  28
#define THEME_TOUCH_MIN     56   /* a wall device is touched imprecisely */
/* Drawn height of every draggable track (volume, volume cap, scrub). A
 * shared token rather than three local copies because "they all look and
 * feel the same" is a real, stated requirement, not a coincidence -- the
 * scrub bar started at the design spec's 3px and was raised to match the
 * volume bars once it became a real drag target rather than a display.
 * Pair with THEME_TOUCH_MIN for the hit band: the visual stays slim, the
 * touch area is (THEME_TOUCH_MIN - THEME_TRACK_H) / 2 of padding each
 * side -- the same draw-smaller-than-tappable rule every control here
 * follows. */
#define THEME_TRACK_H       14

#define THEME_SPACE_XS   4
#define THEME_SPACE_S    8
#define THEME_SPACE_M    16
#define THEME_SPACE_L    24
#define THEME_SPACE_XL   32

#define THEME_MOTION_INSTANT_MS   100   /* press feedback */
#define THEME_MOTION_QUICK_MS     200   /* toggles, toasts */
#define THEME_MOTION_STANDARD_MS  180   /* screen transitions -- was 320ms, reserved but never
                                            wired up, until the Art-forward redesign's page-
                                            edge-tab mechanism briefly became this constant's
                                            first real consumer (repurposed to the design
                                            system's own 180ms page-transition value). Real-
                                            hardware feedback then found even that fixed,
                                            one-shot animation choppy on this board's real
                                            render path and asked for screen transitions to be
                                            instant instead (ui_router_go_to_screen() now uses
                                            LV_ANIM_OFF, no duration at all) -- unused again as
                                            of that change, left defined/reserved rather than
                                            deleted, same as it sat for most of this project's
                                            history before this constant's brief real use. */
#define THEME_MOTION_SLOW_MS      600   /* artwork crossfade */
#define THEME_MOTION_AMBIENT_MS  1200   /* backdrop drift */

#ifdef __cplusplus
}
#endif

#endif /* THEME_METRICS_H */
