/*
 * See app_wiring_volume.h. Split out of app_wiring_ticks.c for the
 * 400-line gate.
 */
#include "app_wiring_volume.h"

#include <string.h>

#include "denon_codec.h"
#include "denon_intent.h"
#include "spotify_intent.h"

intent_t app_wiring_resolve_multi_intent(app_ctx_t *ctx, const intent_t *intent)
{
    intent_t invalid = { .domain = INTENT_DOMAIN_COUNT };

    if (intent->kind != MULTI_INTENT_SET_VOLUME) {
        return invalid;   /* the only real MULTI case today -- an unrecognized
                             * kind is dropped defensively, not asserted, since
                             * this is a runtime dispatch path, not a startup
                             * table build */
    }

    multi_intent_payload_t payload;
    memcpy(&payload, intent->payload, sizeof(payload));

    denon_state_t     denon    = { 0 };
    spotify_state_t   spotify  = { 0 };
    playback_state_t  playback = { 0 };
    core_state_read(&ctx->state, DOMAIN_DENON, &denon, sizeof(denon));
    core_state_read(&ctx->state, DOMAIN_SPOTIFY, &spotify, sizeof(spotify));
    core_state_read(&ctx->state, DOMAIN_PLAYBACK, &playback, sizeof(playback));

    volume_target_resolution_t resolution = resolve_volume_target(&denon, &spotify, &playback);

    intent_t resolved = { 0 };
    if (resolution.domain == VOLUME_TARGET_DENON) {
        resolved.domain = INTENT_DOMAIN_DENON;
        resolved.kind   = DENON_INTENT_SET_VOLUME;
        denon_intent_payload_t denon_payload = { .volume_half_db = payload.volume_half_db };
        memcpy(resolved.payload, &denon_payload, sizeof(denon_payload));
    } else {
        resolved.domain = INTENT_DOMAIN_PLAYBACK;
        resolved.kind   = PLAYBACK_INTENT_SET_VOLUME;
        /* Approximate by design -- Spotify's 0-100% has no fixed
         * relationship to dB, and the destination device's own gain curve
         * is unknown to us (Phase 4 plan §7). Adequate for the case
         * adr/0007 itself calls "unusual." */
        uint32_t pct = ((uint32_t)payload.volume_half_db * 100u) / DENON_MV_HALF_DB_MAX;
        if (pct > 100u) {
            pct = 100u;
        }
        playback_intent_payload_t playback_payload = { .volume_pct = (uint8_t)pct };
        memcpy(resolved.payload, &playback_payload, sizeof(playback_payload));
    }
    return resolved;
}
