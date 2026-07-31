#include "volume_coordinator.h"

#include <stddef.h>

volume_target_resolution_t resolve_volume_target(const denon_state_t *denon,
                                                    const spotify_state_t *spotify,
                                                    const playback_state_t *playback)
{
    (void)denon;
    (void)spotify;

    volume_target_resolution_t out = { .domain = VOLUME_TARGET_DENON, .confident = false };

    if (playback == NULL) {
        return out;
    }

    out.confident = (playback->device_is_avr != DEVICE_IS_AVR_UNKNOWN);

    if (playback->device_is_avr == DEVICE_IS_AVR_NO &&
        !playback->device_is_restricted &&
        playback->device_supports_volume) {
        out.domain = VOLUME_TARGET_PLAYBACK;
    }

    return out;
}
