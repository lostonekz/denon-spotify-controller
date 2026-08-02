/*
 * intent_t.kind/payload when intent_t.domain == INTENT_DOMAIN_PLAYBACK.
 *
 * Lives in core_types (layer "core"), not in svc_spotify's own header where
 * it was originally defined -- Phase 5 Tier 6: `ui` needs to construct
 * these exact values to submit PLAY/PAUSE/NEXT/PREVIOUS from w_transport,
 * but `ui` may not include any svc_* header (tools/arch_rules.json).
 * Moved, not duplicated: spotify_intent.c's spotify_intent_to_cmd() casts
 * intent->kind straight to this enum with no independent range check
 * beyond its own switch -- two separately-maintained copies could drift
 * numerically with no compile-time or runtime signal, silently dispatching
 * the wrong action. spotify_intent.h includes this file as its own
 * canonical definition, the same way intent_types.h's intent_domain_t is
 * already shared this way.
 */
#ifndef CORE_PLAYBACK_INTENT_TYPES_H
#define CORE_PLAYBACK_INTENT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYBACK_INTENT_PLAY = 0,
    PLAYBACK_INTENT_PAUSE,
    PLAYBACK_INTENT_NEXT,
    PLAYBACK_INTENT_PREVIOUS,
    PLAYBACK_INTENT_SET_SHUFFLE,
    PLAYBACK_INTENT_SET_REPEAT,
    PLAYBACK_INTENT_SET_VOLUME,
} playback_intent_kind_t;

typedef struct {
    uint8_t  volume_pct;         /* PLAYBACK_INTENT_SET_VOLUME -- 0-100, clamped by spotify_intent.c */
    bool     shuffle;            /* PLAYBACK_INTENT_SET_SHUFFLE */
    uint8_t  repeat_mode;        /* PLAYBACK_INTENT_SET_REPEAT -- matches playback_state_t's representation */
} playback_intent_payload_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_PLAYBACK_INTENT_TYPES_H */
