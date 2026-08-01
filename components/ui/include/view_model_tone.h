/*
 * tone_t -- the honest freshness signal Now Playing (and, reused, the
 * Denon volume badge) renders against. UI_ARCHITECTURE.md §1 references
 * this type inline as a "tone_t tone" field commented LIVE | STALE |
 * OFFLINE, but never actually defines it -- this is that definition, plus
 * the mapping from domain_meta_t.link_status_t (Phase 5 plan §4).
 *
 * Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 */
#ifndef VIEW_MODEL_TONE_H
#define VIEW_MODEL_TONE_H

#include "app_state_types.h"   /* link_status_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TONE_LIVE = 0,
    TONE_STALE,
    TONE_OFFLINE,
} tone_t;

/* link_status_t is 4 states (OK|STALE|RECONNECTING|UNAVAILABLE); tone_t is
 * 3 (LIVE|STALE|OFFLINE) -- a real mismatch between what every domain
 * actually reports and what the UI renders against. Mapping:
 *   OK           -> LIVE
 *   STALE        -> STALE
 *   RECONNECTING -> STALE   (a retry in flight means the system hasn't
 *                             given up -- telling the user "offline" mid-
 *                             retry is less honest, not more)
 *   UNAVAILABLE  -> OFFLINE
 * Exhaustive, no default: case, so a future 5th link_status_t value is a
 * -Wswitch warning here rather than a silently-absorbed tone. Reused
 * as-is for the Denon volume badge's own honesty check -- not a second
 * bespoke mapping. */
tone_t view_model_tone_from_link_status(link_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_TONE_H */
