/*
 * Progress interpolation and elapsed/duration -> permille conversion for
 * Now Playing. Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 *
 * view_model_progress_displayed_ms() duplicates adr/0004's local-progress-
 * interpolation formula rather than depending on svc_spotify's own
 * spotify_progress.c: that header lives in svc_spotify (layer "svc"),
 * forbidden to `ui` (layer "ui") by tools/arch_rules.json -- confirmed
 * directly, check_includes.py rejects the edge when tried. The formula
 * itself is small (one clamp, one subtraction), so this is the same
 * duplicate-across-the-layer-boundary treatment this codebase already
 * gives percent_encode()/safe_strncpy()/view_model_format_half_db() at
 * similar boundaries, not a new pattern.
 */
#ifndef VIEW_MODEL_PROGRESS_H
#define VIEW_MODEL_PROGRESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* adr/0004: displayed_ms = progress_ms + (is_playing ? (now_us -
 * sampled_at_us)/1000 : 0). An optimistic pause freezes interpolation
 * immediately (caller passes is_playing=false); a fix timestamped in the
 * future (should not happen, defended anyway) clamps elapsed to 0 rather
 * than going negative. */
uint32_t view_model_progress_displayed_ms(uint32_t progress_ms, int64_t sampled_at_us,
                                            bool is_playing, int64_t now_us);

/* duration_ms == 0 (no known duration yet, or a genuinely zero-length
 * item) returns 0 rather than dividing by zero. elapsed_ms >= duration_ms
 * (a brief interpolation overshoot near track end, before the next poll
 * corrects it) clamps to 1000 -- never wraps, never exceeds the 0..1000
 * range in either direction. */
uint16_t view_model_progress_permille(uint32_t elapsed_ms, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_PROGRESS_H */
