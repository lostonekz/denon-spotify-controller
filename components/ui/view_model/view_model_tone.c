#include "view_model_tone.h"

tone_t view_model_tone_from_link_status(link_status_t status)
{
    switch (status) {
    case LINK_STATUS_OK:           return TONE_LIVE;
    case LINK_STATUS_STALE:        return TONE_STALE;
    case LINK_STATUS_RECONNECTING: return TONE_STALE;
    case LINK_STATUS_UNAVAILABLE:  return TONE_OFFLINE;
    }
    return TONE_OFFLINE;   /* unreachable if the switch above stays exhaustive */
}
