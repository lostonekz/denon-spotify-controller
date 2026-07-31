/*
 * Adaptive poll-interval policy. docs/NETWORKING.md §1.3's table, exactly:
 *
 *   Playing, screen awake    5 s
 *   Playing, screen asleep  30 s   (keeps wake-on-playback working)
 *   Paused, awake           10 s
 *   Paused, asleep          60 s
 *   After a user intent     force poll at +250ms, +1s
 *   HTTP 429                honour Retry-After exactly (net_http's own job,
 *                            not this file's -- see net_http_retry_policy.h)
 *   Consecutive errors      exponential backoff to 120s cap
 *
 * Pure -- fake-clock tested. The "asleep" conditions are exercised here
 * with no real producer yet (svc_power doesn't exist until Phase 6) --
 * same treatment Phase 3 gave denon_link_fsm's dead SLEEP_ENTER/WAKE pair;
 * callers hardcode screen_awake=true until then.
 */
#ifndef SPOTIFY_POLL_POLICY_H
#define SPOTIFY_POLL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_playing;
    bool screen_awake;
} spotify_poll_condition_t;

/* Microseconds until the next routine poll, per the table above. */
int64_t spotify_poll_interval_us(const spotify_poll_condition_t *cond);

/* Force-poll deadlines after a user intent (+250ms, +1s) -- index 0 and 1
 * respectively. Any other index returns 0 (no further force-poll). */
#define SPOTIFY_POLL_FORCE_POLL_COUNT 2
int64_t spotify_poll_force_deadline_us(int64_t intent_submitted_at_us, int index);

/* Exponential backoff after N consecutive poll errors, base 5s (the
 * fastest routine interval -- not specified by any doc, a judgement call),
 * doubling per additional error, capped at 120s. consecutive_errors == 0
 * returns 0 (no error streak in progress). */
int64_t spotify_poll_backoff_us(uint32_t consecutive_errors);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_POLL_POLICY_H */
