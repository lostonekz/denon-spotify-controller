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
#include "multi_intent_types.h"   /* multi_intent_kind_t/multi_intent_payload_t's
                                      canonical definition -- moved to core_types
                                      so `ui`'s w_volume_slider can construct
                                      them too; see that header's own comment */

#ifdef __cplusplus
extern "C" {
#endif

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
