#include "spotify_progress.h"

#include <stddef.h>

uint32_t spotify_progress_displayed_ms(const playback_fix_t *fix, time_source_t clock)
{
    if (fix == NULL) {
        return 0;
    }
    if (!fix->is_playing) {
        return fix->progress_ms;
    }

    int64_t now_us = clock.now_us(clock.ctx);
    int64_t elapsed_ms = (now_us - fix->sampled_at_us) / 1000;
    if (elapsed_ms < 0) {
        elapsed_ms = 0;   /* defensive: a fix timestamped in the future shouldn't happen */
    }
    return fix->progress_ms + (uint32_t)elapsed_ms;
}
