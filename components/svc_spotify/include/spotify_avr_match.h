/*
 * Decides whether the active Spotify Connect device is the pinned AVR.
 * Pure -- no I/O; the caller (spotify_service.c) is responsible for
 * fetching whatever data this needs and passing it in.
 *
 * Deliberately NOT derived from Spotify's device `type` field ("AVR" is
 * not a documented value) -- matches against a user-pinned device id/name
 * instead (`spotify set-avr-device`). Neither id nor name is trusted alone:
 * Spotify documents device ids as not guaranteed stable, and device names
 * are not guaranteed unique, so this module needs both plus, in the
 * ambiguous case, the full device list to tell "the AVR got a new id"
 * apart from "some other device happens to share its name."
 *
 * See docs/adr/0007-denon-volume-is-primary.md and this project's Phase 4
 * plan §3 for the full reasoning and the three failure modes this design
 * specifically closes (type taxonomy, id instability, name uniqueness).
 */
#ifndef SPOTIFY_AVR_MATCH_H
#define SPOTIFY_AVR_MATCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_id;     /* may be NULL/empty -- Spotify allows this */
    const char *device_name;
} spotify_device_ref_t;

typedef enum {
    /* Nothing pinned yet, no active device at all, or a name-match
     * candidate that couldn't be disambiguated (old id still present
     * elsewhere in the list, multiple devices share the pinned name, or no
     * device list was available to check) -- "we cannot confidently tell,"
     * matching adr/0007's own stated safe fallback. */
    SPOTIFY_AVR_MATCH_UNKNOWN = 0,
    SPOTIFY_AVR_MATCH_YES,
    SPOTIFY_AVR_MATCH_NO,
} spotify_avr_match_result_t;

typedef struct {
    spotify_avr_match_result_t result;
    /* Non-NULL only when result == YES via the name-based fallback with a
     * changed id -- the caller should persist this as the new pinned
     * device id (spotify_avr_pin_save). Points into the caller-owned
     * `all_devices` array passed below; valid only for the duration of
     * this call, not retained past it. */
    const char *self_heal_device_id;
} spotify_avr_match_outcome_t;

/* all_devices/device_count: the most recent GET /v1/me/player/devices
 * listing, or NULL/0 if not fetched (fine on the id-match fast path; the
 * name-match fallback needs it to check for ambiguity and returns UNKNOWN
 * rather than guess if it's unavailable). */
spotify_avr_match_outcome_t spotify_avr_match_resolve(
    const char *active_device_id, const char *active_device_name,
    const char *pinned_device_id, const char *pinned_device_name,
    const spotify_device_ref_t *all_devices, size_t device_count);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_AVR_MATCH_H */
