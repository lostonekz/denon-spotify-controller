/*
 * Resolves where a volume intent should go: the Denon AVR (default) or the
 * active Spotify Connect device (only when confidently confirmed to be
 * something else, and actually controllable). docs/adr/0007 and
 * docs/EVENTS_AND_STATE.md §5.1: INTENT_DOMAIN_MULTI has no single owning
 * service, so this lives in main/ (app_wiring's mediation layer), not any
 * svc_* component -- svc_denon and svc_spotify may not depend on each
 * other (no svc->svc edge).
 *
 * Pure -- the first host-tested module living in main/ rather than
 * components/, since this coordinator itself has no other home.
 */
#ifndef VOLUME_COORDINATOR_H
#define VOLUME_COORDINATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* intent_t.kind when intent_t.domain == INTENT_DOMAIN_MULTI. Today exactly
 * one case exists (adr/0007's volume retargeting) -- EVENTS_AND_STATE.md
 * §5.1's own framing: "a missing table entry is a startup assertion." */
typedef enum {
    MULTI_INTENT_SET_VOLUME = 0,
} multi_intent_kind_t;

typedef struct {
    /* Denon's own half-dB scale -- the common path (target == DENON) is a
     * zero-conversion pass-through into DENON_INTENT_SET_VOLUME; only the
     * rare retarget case (target == PLAYBACK) pays an approximate
     * half_db->pct conversion cost. See app_wiring_volume.c. */
    uint16_t volume_half_db;
} multi_intent_payload_t;

typedef enum {
    VOLUME_TARGET_DENON = 0,
    VOLUME_TARGET_PLAYBACK,
} volume_target_domain_t;

typedef struct {
    volume_target_domain_t domain;
    /* Reflects whether playback->device_is_avr itself was confidently
     * YES/NO (true) vs UNKNOWN (false) -- independent of whether a
     * confirmed non-AVR device turned out to be a viable retarget
     * candidate (is_restricted/supports_volume). A confident "it's the
     * AVR" and a confident-but-non-viable "it's some other device that
     * can't take volume commands" are both confident identifications that
     * happen to resolve to VOLUME_TARGET_DENON, for different reasons. */
    bool confident;
} volume_target_resolution_t;

/* denon/spotify are accepted for future extensibility (matching the shape
 * a cross-domain coordinator generally needs) but unused this phase --
 * Phase 4's own resolution logic only inspects playback's fields. Denon's
 * own reachability (link up/down) is a separate concern already handled
 * by svc_denon's own link FSM degrading independently; conflating "who
 * owns the volume" with "can we currently reach them" would blur two
 * failure modes this architecture keeps apart. */
volume_target_resolution_t resolve_volume_target(const denon_state_t *denon,
                                                    const spotify_state_t *spotify,
                                                    const playback_state_t *playback);

#ifdef __cplusplus
}
#endif

#endif /* VOLUME_COORDINATOR_H */
