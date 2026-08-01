#include "view_model_progress.h"

uint32_t view_model_progress_displayed_ms(uint32_t progress_ms, int64_t sampled_at_us,
                                            bool is_playing, int64_t now_us)
{
    if (!is_playing) {
        return progress_ms;
    }

    int64_t elapsed_ms = (now_us - sampled_at_us) / 1000;
    if (elapsed_ms < 0) {
        elapsed_ms = 0;   /* defensive: a fix timestamped in the future shouldn't happen */
    }
    return progress_ms + (uint32_t)elapsed_ms;
}

uint16_t view_model_progress_permille(uint32_t elapsed_ms, uint32_t duration_ms)
{
    if (duration_ms == 0u) {
        return 0u;
    }
    if (elapsed_ms >= duration_ms) {
        return 1000u;
    }

    uint64_t permille = ((uint64_t)elapsed_ms * 1000u) / duration_ms;
    return (uint16_t)permille;
}
