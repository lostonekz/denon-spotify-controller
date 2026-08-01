/*
 * Pure decision logic for Phase 5 Tier -1's intent-ack fix
 * (docs/EVENTS_AND_STATE.md §6.3's optimistic-UI protocol). Phase 4's own
 * plan called for svc_spotify to publish INTENT_ACK_EVT_ACCEPTED/REJECTED
 * on TOPIC_INTENT_ACK, but a repo-wide search found the type and topic
 * policy wired and zero publishers -- this was planned, never shipped.
 *
 * Deliberately extracted from spotify_service_task.c's own target-only
 * handle_playback_intent(): everything else in that file embeds a
 * net_http_t/net_time_t pointer and is never host-tested (spotify_service.h's
 * own top comment), but "which ack kind for this API result" is pure
 * data-in/data-out and deserves its own real host test rather than going
 * unverified along with the rest of that file -- the same "extract the pure
 * kernel" treatment this project already gave svc_ota's
 * ota_download_would_overflow() predicate.
 */
#ifndef SPOTIFY_INTENT_ACK_H
#define SPOTIFY_INTENT_ACK_H

#include "event_types.h"          /* intent_ack_evt_kind_t */
#include "spotify_api_result.h"   /* spotify_api_result_t -- deliberately not
                                     * spotify_api.h, which pulls in net_http.h's
                                     * FreeRTOS dependency and would make this
                                     * module target-only for no real reason */

#ifdef __cplusplus
extern "C" {
#endif

/* SPOTIFY_API_RESULT_OK -> ACCEPTED; every other result (NO_ACTIVE_DEVICE,
 * UNAUTHORIZED, FAILED) -> REJECTED. NO_ACTIVE_DEVICE is a real command
 * failure from the intent's own point of view even though it's not an
 * error for a plain status poll -- there was nothing to play/pause/skip.
 * INTENT_ACK_EVT_SUPERSEDED is never returned here: the playback-intent
 * queue still has no coalescing (Phase 4 plan §4/§5), so nothing supersedes
 * anything yet -- same "kept for the vocabulary, no producer yet"
 * treatment as denon_link_fsm's own dead SLEEP_ENTER/WAKE pair. */
intent_ack_evt_kind_t spotify_intent_ack_kind_for_result(spotify_api_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_INTENT_ACK_H */
