/*
 * Intent -> internal API command translation. See spotify_intent.h.
 * No allocation, no I/O, no ESP-IDF. Host-tested (test/host/test_spotify_intent.c).
 */
#include "spotify_intent.h"

#include <string.h>

spotify_intent_result_t spotify_intent_to_cmd(const intent_t *intent, spotify_api_cmd_t *out_cmd)
{
    if (intent == NULL || out_cmd == NULL) {
        return SPOTIFY_INTENT_RESULT_UNKNOWN_KIND;
    }
    if (intent->domain != INTENT_DOMAIN_PLAYBACK) {
        return SPOTIFY_INTENT_RESULT_WRONG_DOMAIN;
    }

    playback_intent_payload_t payload;
    memcpy(&payload, intent->payload, sizeof(payload));

    switch ((playback_intent_kind_t)intent->kind) {
    case PLAYBACK_INTENT_PLAY:
        out_cmd->kind = SPOTIFY_API_CMD_PLAY;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_PAUSE:
        out_cmd->kind = SPOTIFY_API_CMD_PAUSE;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_NEXT:
        out_cmd->kind = SPOTIFY_API_CMD_NEXT;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_PREVIOUS:
        out_cmd->kind = SPOTIFY_API_CMD_PREVIOUS;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_SET_SHUFFLE:
        out_cmd->kind        = SPOTIFY_API_CMD_SET_SHUFFLE;
        out_cmd->u.shuffle = payload.shuffle;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_SET_REPEAT:
        out_cmd->kind             = SPOTIFY_API_CMD_SET_REPEAT;
        out_cmd->u.repeat_mode = payload.repeat_mode;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_SET_VOLUME:
        out_cmd->kind          = SPOTIFY_API_CMD_SET_VOLUME;
        /* Spotify's device-volume scale is 0-100; clamp at this protocol
         * boundary rather than reject, same discipline as denon_intent.c's
         * safety clamp (adr/0007 / RISKS.md R13), just without a safety
         * dimension here -- an out-of-range request is a bug upstream, not
         * something the user should see rejected outright. */
        out_cmd->u.volume_pct = (payload.volume_pct > 100) ? 100 : payload.volume_pct;
        return SPOTIFY_INTENT_RESULT_OK;
    case PLAYBACK_INTENT_SEEK:
        out_cmd->kind          = SPOTIFY_API_CMD_SEEK;
        out_cmd->u.position_ms = payload.seek_position_ms;
        return SPOTIFY_INTENT_RESULT_OK;
    default:
        return SPOTIFY_INTENT_RESULT_UNKNOWN_KIND;
    }
}
