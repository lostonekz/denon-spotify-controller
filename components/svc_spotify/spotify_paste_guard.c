/* See spotify_paste_guard.h for why these decisions live apart from their
 * two callers, and why an untested one fails silently rather than loudly. */

#include "spotify_paste_guard.h"

#include <string.h>

bool spotify_paste_extract_param(const char *url, const char *key, char *out, size_t out_cap)
{
    if (url == NULL || key == NULL || out == NULL || out_cap == 0) {
        return false;
    }

    const char *q = strchr(url, '?');
    if (q == NULL) {
        return false;
    }
    q++;
    size_t key_len = strlen(key);
    while (*q != '\0') {
        const char *amp     = strchr(q, '&');
        size_t      seg_len = (amp != NULL) ? (size_t)(amp - q) : strlen(q);
        /* seg_len > key_len rather than >=: it also rejects a bare `code`
         * with no `=`, which would otherwise index q[key_len] at the
         * segment's own terminator. */
        if (seg_len > key_len && q[key_len] == '=' && strncmp(q, key, key_len) == 0) {
            size_t val_len = seg_len - key_len - 1;
            if (val_len >= out_cap) {
                val_len = out_cap - 1;
            }
            memcpy(out, q + key_len + 1, val_len);
            out[val_len] = '\0';
            return true;
        }
        if (amp == NULL) {
            break;
        }
        q = amp + 1;
    }
    return false;
}

bool spotify_paste_state_matches(const char *pending_state, const char *state_param)
{
    if (pending_state == NULL || state_param == NULL) {
        return false;
    }
    /* No attempt in flight means nothing to match against -- see the header
     * for why this empty case is the security-relevant one. */
    if (pending_state[0] == '\0') {
        return false;
    }
    return strcmp(pending_state, state_param) == 0;
}

spotify_paste_precondition_t spotify_paste_precondition(bool clock_synced,
                                                        spotify_session_summary_t session_status)
{
    if (!clock_synced) {
        return SPOTIFY_PASTE_PRECONDITION_CLOCK_NOT_SYNCED;
    }
    if (session_status != SPOTIFY_SESSION_PAIRING) {
        return SPOTIFY_PASTE_PRECONDITION_SESSION_NOT_AWAITING;
    }
    return SPOTIFY_PASTE_PRECONDITION_OK;
}
