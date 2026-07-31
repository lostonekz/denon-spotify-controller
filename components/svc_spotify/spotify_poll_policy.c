#include "spotify_poll_policy.h"

#include <stddef.h>

#define US_PER_S ((int64_t)1000000)

#define SPOTIFY_POLL_PLAYING_AWAKE_US   ((int64_t)5  * US_PER_S)
#define SPOTIFY_POLL_PLAYING_ASLEEP_US  ((int64_t)30 * US_PER_S)
#define SPOTIFY_POLL_PAUSED_AWAKE_US    ((int64_t)10 * US_PER_S)
#define SPOTIFY_POLL_PAUSED_ASLEEP_US   ((int64_t)60 * US_PER_S)

#define SPOTIFY_POLL_FORCE_DEADLINE_0_US ((int64_t)250 * 1000)
#define SPOTIFY_POLL_FORCE_DEADLINE_1_US ((int64_t)1   * US_PER_S)

#define SPOTIFY_POLL_BACKOFF_BASE_US ((int64_t)5   * US_PER_S)
#define SPOTIFY_POLL_BACKOFF_CAP_US  ((int64_t)120 * US_PER_S)

int64_t spotify_poll_interval_us(const spotify_poll_condition_t *cond)
{
    if (cond == NULL) {
        return SPOTIFY_POLL_PLAYING_AWAKE_US;   /* most conservative (fastest) default */
    }
    if (cond->is_playing) {
        return cond->screen_awake ? SPOTIFY_POLL_PLAYING_AWAKE_US : SPOTIFY_POLL_PLAYING_ASLEEP_US;
    }
    return cond->screen_awake ? SPOTIFY_POLL_PAUSED_AWAKE_US : SPOTIFY_POLL_PAUSED_ASLEEP_US;
}

int64_t spotify_poll_force_deadline_us(int64_t intent_submitted_at_us, int index)
{
    switch (index) {
    case 0:  return intent_submitted_at_us + SPOTIFY_POLL_FORCE_DEADLINE_0_US;
    case 1:  return intent_submitted_at_us + SPOTIFY_POLL_FORCE_DEADLINE_1_US;
    default: return 0;
    }
}

int64_t spotify_poll_backoff_us(uint32_t consecutive_errors)
{
    if (consecutive_errors == 0) {
        return 0;
    }

    int64_t interval = SPOTIFY_POLL_BACKOFF_BASE_US;
    for (uint32_t i = 1; i < consecutive_errors && interval < SPOTIFY_POLL_BACKOFF_CAP_US; i++) {
        interval *= 2;
    }
    return (interval > SPOTIFY_POLL_BACKOFF_CAP_US) ? SPOTIFY_POLL_BACKOFF_CAP_US : interval;
}
