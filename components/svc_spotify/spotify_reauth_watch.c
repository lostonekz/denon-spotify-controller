#include "spotify_reauth_watch.h"

/* ~5.5 months, a defensive margin before Spotify's own ~6-month refresh
 * token ceiling -- 165 days * 86400 s/day. Not specified anywhere by
 * Spotify itself (they document ~6 months, not a warning point), a
 * judgement call to leave a real margin before the token stops working
 * outright. */
#define SPOTIFY_REAUTH_WARN_THRESHOLD_S ((int64_t)165 * 24 * 3600)

spotify_reauth_status_t spotify_reauth_check(int64_t paired_at_epoch_s, int64_t now_epoch_s)
{
    if (paired_at_epoch_s <= 0 || now_epoch_s < paired_at_epoch_s) {
        return SPOTIFY_REAUTH_UNKNOWN;
    }

    int64_t age_s = now_epoch_s - paired_at_epoch_s;
    return (age_s >= SPOTIFY_REAUTH_WARN_THRESHOLD_S) ? SPOTIFY_REAUTH_DUE_SOON
                                                          : SPOTIFY_REAUTH_NOT_DUE;
}
