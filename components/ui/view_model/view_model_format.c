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

/* System screen's "Awake for" -- days+hours once past a day (e.g. "6 d
 * 4 h"), otherwise hours+minutes (e.g. "2 h 14 m"), otherwise just minutes
 * (e.g. "45 m") -- the coarser unit is dropped entirely rather than shown
 * as zero, matching view_model_format_mmss()'s own "no leading zero"
 * spirit. */
void view_model_format_uptime(uint32_t total_seconds, char *buf, size_t buflen)
{
    if (buflen == 0) {
        return;
    }
    uint32_t minutes = total_seconds / 60u;
    uint32_t hours   = minutes / 60u;
    uint32_t days    = hours / 24u;

    if (days > 0) {
        snprintf(buf, buflen, "%u d %u h", (unsigned)days, (unsigned)(hours % 24u));
    } else if (hours > 0) {
        snprintf(buf, buflen, "%u h %u m", (unsigned)hours, (unsigned)(minutes % 60u));
    } else {
        snprintf(buf, buflen, "%u m", (unsigned)minutes);
    }
}
