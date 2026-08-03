/*
 * Display-string formatters for Now Playing -- ms -> "3:07"/"-1:24",
 * half-dB -> "52.5". Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 */
#ifndef VIEW_MODEL_FORMAT_H
#define VIEW_MODEL_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "3:07" -- ms truncated to whole seconds, m:ss, no leading zero on
 * minutes, seconds always 2 digits. Always NUL-terminates (buflen must be
 * >= 1); truncates rather than overflows if buflen is smaller than the
 * formatted result needs (an absurdly long "track," not a real case this
 * project expects, but never a buffer overrun either way). */
void view_model_format_mmss(uint32_t ms, char *buf, size_t buflen);

/* "-1:24" -- always prefixed with '-' by convention (this is "time
 * remaining," a display idiom, not a negative duration). remaining_ms =
 * duration_ms - elapsed_ms, clamped to 0 if elapsed_ms >= duration_ms (a
 * brief interpolation overshoot near track end, before the next poll
 * corrects it, must never show as a jump to a huge or negative number). */
void view_model_format_remaining_mmss(uint32_t elapsed_ms, uint32_t duration_ms,
                                        char *buf, size_t buflen);

/* "52.5" -- half_db/2 . (half_db%2)*5. Matches denon_codec.h's own
 * denon_mv_to_display() output exactly, but deliberately NOT a call into
 * it: denon_codec.h lives in svc_denon (layer "svc"), forbidden to `ui`
 * (layer "ui") by tools/arch_rules.json. Duplicated across that layer
 * boundary on purpose -- the same treatment this codebase already gives
 * percent_encode()/safe_strncpy() at similar boundaries. */
void view_model_format_half_db(uint16_t half_db, char *buf, size_t buflen);

/* "6 d 4 h" / "2 h 14 m" / "45 m" -- System screen's "Awake for". Always
 * NUL-terminates, same contract as the other formatters here. */
void view_model_format_uptime(uint32_t total_seconds, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_FORMAT_H */
