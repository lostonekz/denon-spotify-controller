/*
 * The accept/reject decisions behind a pasted Spotify redirect URL. Pure --
 * no I/O, no locks; every caller fetches what these need and passes it in.
 *
 * WHY THIS IS A SEPARATE UNIT: these three checks are the pairing flow's
 * only rejection paths, and until this file existed none of them had a host
 * test. They could not have one. Two of them lived inside
 * svc_spotify_submit_pasted_url(), which takes svc_spotify_t -- a struct
 * embedding net_http_t and net_time_t handles, so test/host cannot compile
 * anything that references it (spotify_service_state.h's top comment
 * records the same constraint for the same reason). The third lived in
 * main/pair_http.c behind esp_http_server. Extracting the decisions leaves
 * the I/O where it was and makes the decisions testable.
 *
 * WHY IT MATTERS MORE THAN THE USUAL COVERAGE ARGUMENT: these are the paths
 * where a regression is SILENT. svc_spotify_submit_pasted_url() returns
 * true in cases that then do nothing at all -- a CODE_PASTED command is
 * dropped later on the spotify task when the clock is unset, and
 * spotify_session_fsm ignores CODE_PASTED outside AWAITING_CODE without
 * logging. A broken check here does not fail loudly; it tells the user
 * their pairing worked when nothing happened. See docs/ROADMAP.md's Phase 6
 * exit note, which names these three as the unverified ones.
 *
 * Checks stay separate rather than collapsing into one entry point because
 * the two callers legitimately know different things: svc_spotify has the
 * pending nonce but no view of the clock or the session summary, while
 * pair_http has both and runs them before it ever calls the service.
 */
#ifndef SPOTIFY_PASTE_GUARD_H
#define SPOTIFY_PASTE_GUARD_H

#include <stdbool.h>
#include <stddef.h>

#include "app_state_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reads one `key=value` out of a URL's query string into `out`.
 *
 * Values longer than `out_cap` are TRUNCATED, not rejected -- preserved
 * from the original implementation deliberately. A truncated `state` then
 * fails spotify_paste_state_matches() and a truncated `code` fails the
 * exchange upstream, so both over-long inputs still end in a rejection
 * rather than a buffer overrun.
 *
 * Matches only a full segment key: `code=x` matches "code", `barcode=x`
 * does not. Returns false when there is no `?`, no such key, or the key
 * appears with no `=`.
 */
bool spotify_paste_extract_param(const char *url, const char *key, char *out, size_t out_cap);

/* The stale-link check: does the `state` nonce from the pasted URL match
 * the one minted for the pairing attempt still in flight?
 *
 * An empty `pending_state` is never a match, which is the case that makes
 * this a security check and not merely a UX one: with no attempt in flight
 * there is no nonce to replay against, so accepting any state at all would
 * reopen the replay window that the Phase 6 re-pair work closed. Both
 * arguments may be NULL; NULL never matches.
 */
bool spotify_paste_state_matches(const char *pending_state, const char *state_param);

typedef enum {
    SPOTIFY_PASTE_PRECONDITION_OK = 0,
    /* SNTP has not synced yet. The resulting CODE_PASTED command is dropped
     * silently on the spotify task -- the token exchange needs a real clock
     * to validate TLS certificates against. */
    SPOTIFY_PASTE_PRECONDITION_CLOCK_NOT_SYNCED,
    /* The session is not awaiting a code. spotify_session_fsm ignores
     * CODE_PASTED outside AWAITING_CODE with no log at all, so submitting
     * anyway reports a success that never happens. */
    SPOTIFY_PASTE_PRECONDITION_SESSION_NOT_AWAITING,
} spotify_paste_precondition_t;

/* Both checks a caller must clear BEFORE submitting a paste, in the order
 * they are reported. Order is not arbitrary: an unsynced clock is the
 * transient one a user fixes by waiting a few seconds, so it is worth
 * saying first when both are true.
 *
 * SPOTIFY_SESSION_PAIRING is the only accepting status. It collapses the
 * FSM's AWAITING_CODE and EXCHANGING_CODE (app_state_types.h), so a paste
 * arriving while an earlier exchange is still in flight is accepted here
 * and then ignored by the FSM -- harmless, and preserved from the original
 * behaviour rather than tightened, because narrowing it would need a state
 * the summary enum deliberately does not expose.
 */
spotify_paste_precondition_t spotify_paste_precondition(bool clock_synced,
                                                        spotify_session_summary_t session_status);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_PASTE_GUARD_H */
