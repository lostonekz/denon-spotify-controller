#include "spotify_intent_ack.h"

intent_ack_evt_kind_t spotify_intent_ack_kind_for_result(spotify_api_result_t r)
{
    switch (r) {
    case SPOTIFY_API_RESULT_OK:
        return INTENT_ACK_EVT_ACCEPTED;
    case SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE:
    case SPOTIFY_API_RESULT_UNAUTHORIZED:
    case SPOTIFY_API_RESULT_FAILED:
        return INTENT_ACK_EVT_REJECTED;
    }
    return INTENT_ACK_EVT_REJECTED;   /* unreachable if the switch above stays exhaustive;
                                        * deliberately no default: case, so a future new
                                        * spotify_api_result_t value is a -Wswitch warning
                                        * here, not a silently-absorbed REJECTED. */
}
