/*
 * adr/0004-local-progress-interpolation.md: displayed_ms = fix.progress_ms
 * + (is_playing ? (now_us - fix.sampled_at_us)/1000 : 0). Interpolates the
 * playback position against the monotonic clock between polls, so the
 * progress bar doesn't visibly stall for up to 5-60s at a time. Uses
 * esp_timer's monotonic clock (via the injected time_source_t), never wall
 * time -- an SNTP step must not jump the bar.
 *
 * Pure and correct, but uncalled this phase: Phase 5's view_model is the
 * real consumer. Host-tested with a fake time source
 * (test/host/fake_time_source.c).
 */
#ifndef SPOTIFY_PROGRESS_H
#define SPOTIFY_PROGRESS_H

#include <stdbool.h>
#include <stdint.h>

#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t progress_ms;      /* as reported by Spotify at the last poll */
    int64_t  sampled_at_us;    /* clock.now_us() at that poll */
    bool     is_playing;
} playback_fix_t;

/* An optimistic pause freezes interpolation immediately (is_playing=false
 * in the fix the caller passes in); if the server disagrees, interpolation
 * resumes from the corrected fix on the next poll -- adr/0004's own stated
 * interaction with optimistic UI. This function does not know about
 * optimism itself, it just reads whatever fix it's given. */
uint32_t spotify_progress_displayed_ms(const playback_fix_t *fix, time_source_t clock);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_PROGRESS_H */
