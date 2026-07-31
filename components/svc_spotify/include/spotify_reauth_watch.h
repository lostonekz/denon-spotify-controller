/*
 * Tracks the Spotify refresh token's own ~6-month outer lifetime -- a
 * separate, longer clock from ordinary access-token refresh (proactive
 * refresh at 80% of `expires_in`, roughly hourly, does not reset or extend
 * it). Wall-clock time, deliberately: a monotonic clock resets to zero on
 * every reboot, and this window will span many reboots (a routine OTA
 * update alone guarantees it). Pure -- takes plain epoch-second values, no
 * dependency on any particular clock source, so it's fully host-tested
 * with fixed values rather than a fake clock.
 */
#ifndef SPOTIFY_REAUTH_WATCH_H
#define SPOTIFY_REAUTH_WATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* paired_at_epoch_s is unset (0), or now_epoch_s < paired_at_epoch_s
     * (an implausible clock-went-backward reading) -- "we cannot
     * confidently tell," never silently reported as NOT_DUE. */
    SPOTIFY_REAUTH_UNKNOWN = 0,
    SPOTIFY_REAUTH_NOT_DUE,
    SPOTIFY_REAUTH_DUE_SOON,
} spotify_reauth_status_t;

spotify_reauth_status_t spotify_reauth_check(int64_t paired_at_epoch_s, int64_t now_epoch_s);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_REAUTH_WATCH_H */
