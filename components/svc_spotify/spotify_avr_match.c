#include "spotify_avr_match.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static bool str_nonempty(const char *s)
{
    return s != NULL && s[0] != '\0';
}

static bool str_eq(const char *a, const char *b)
{
    return str_nonempty(a) && str_nonempty(b) && strcmp(a, b) == 0;
}

static bool str_eq_ci(const char *a, const char *b)
{
    if (!str_nonempty(a) || !str_nonempty(b)) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

spotify_avr_match_outcome_t spotify_avr_match_resolve(
    const char *active_device_id, const char *active_device_name,
    const char *pinned_device_id, const char *pinned_device_name,
    const spotify_device_ref_t *all_devices, size_t device_count)
{
    spotify_avr_match_outcome_t out = { .result = SPOTIFY_AVR_MATCH_UNKNOWN, .self_heal_device_id = NULL };

    bool has_pin    = str_nonempty(pinned_device_id) || str_nonempty(pinned_device_name);
    bool has_active = str_nonempty(active_device_id) || str_nonempty(active_device_name);
    if (!has_pin || !has_active) {
        return out;   /* UNKNOWN: nothing pinned yet, or no active device at all */
    }

    /* 1. id match -- fast path, the common case, no device list needed. */
    if (str_eq(active_device_id, pinned_device_id)) {
        out.result = SPOTIFY_AVR_MATCH_YES;
        return out;
    }

    /* 2. Name match candidate -- Spotify's id is not guaranteed stable, so
     * an id mismatch alone does not mean "not the AVR." But a name match
     * alone isn't safe to trust either (names aren't guaranteed unique) --
     * disambiguate against the full list before ever self-healing. */
    if (str_eq_ci(active_device_name, pinned_device_name)) {
        if (all_devices == NULL) {
            return out;   /* UNKNOWN -- can't disambiguate without the list */
        }

        bool old_id_present = false;
        int  name_match_count = 0;
        const char *sole_name_match_id = NULL;

        for (size_t i = 0; i < device_count; i++) {
            if (str_eq(all_devices[i].device_id, pinned_device_id)) {
                old_id_present = true;
            }
            if (str_eq_ci(all_devices[i].device_name, pinned_device_name)) {
                name_match_count++;
                sole_name_match_id = all_devices[i].device_id;
            }
        }

        if (old_id_present) {
            return out;   /* UNKNOWN -- old pin might still be a real, distinct device */
        }
        if (name_match_count == 1) {
            out.result = SPOTIFY_AVR_MATCH_YES;
            out.self_heal_device_id = sole_name_match_id;
            return out;
        }
        return out;   /* UNKNOWN -- multiple devices share the pinned name */
    }

    /* 3. Neither id nor name matched a non-empty active device. */
    out.result = SPOTIFY_AVR_MATCH_NO;
    return out;
}
