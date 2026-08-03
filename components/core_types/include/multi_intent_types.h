/*
 * intent_t.kind/payload when intent_t.domain == INTENT_DOMAIN_MULTI.
 *
 * Lives in core_types (layer "core"), not in main/volume_coordinator.h where
 * it was originally defined -- Phase 5 redesign: the new w_volume_slider
 * widget needs to construct these exact values to submit a real volume
 * change per adr/0007 ("the one true volume control"), but `ui` may not
 * include anything from `main` (tools/arch_rules.json: ui -> [core, lvgl,
 * idf, ui_hal_glue]). Moved, not duplicated -- same "one canonical
 * definition, no drift risk" reasoning already applied to
 * playback_intent_types.h for the identical svc_spotify/w_transport
 * problem. main/volume_coordinator.h includes this file as its own
 * canonical definition.
 */
#ifndef CORE_MULTI_INTENT_TYPES_H
#define CORE_MULTI_INTENT_TYPES_H

#include <stdint.h>

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
     * half_db->pct conversion cost. See main/app_wiring_volume.c. */
    uint16_t volume_half_db;
} multi_intent_payload_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_MULTI_INTENT_TYPES_H */
