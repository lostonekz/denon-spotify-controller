#include "view_model_format.h"

#include <stdio.h>

void view_model_format_mmss(uint32_t ms, char *buf, size_t buflen)
{
    if (buflen == 0) {
        return;
    }
    uint32_t total_s = ms / 1000u;
    uint32_t minutes = total_s / 60u;
    uint32_t seconds = total_s % 60u;
    snprintf(buf, buflen, "%u:%02u", (unsigned)minutes, (unsigned)seconds);
}

void view_model_format_remaining_mmss(uint32_t elapsed_ms, uint32_t duration_ms,
                                        char *buf, size_t buflen)
{
    if (buflen == 0) {
        return;
    }
    uint32_t remaining_ms = (elapsed_ms >= duration_ms) ? 0u : (duration_ms - elapsed_ms);
    uint32_t total_s = remaining_ms / 1000u;
    uint32_t minutes = total_s / 60u;
    uint32_t seconds = total_s % 60u;
    snprintf(buf, buflen, "-%u:%02u", (unsigned)minutes, (unsigned)seconds);
}

void view_model_format_half_db(uint16_t half_db, char *buf, size_t buflen)
{
    if (buflen == 0) {
        return;
    }
    unsigned whole = (unsigned)(half_db / 2u);
    unsigned frac  = (unsigned)((half_db % 2u) * 5u);
    snprintf(buf, buflen, "%u.%u", whole, frac);
}
