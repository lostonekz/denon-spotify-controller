/*
 * Translates a playback intent into an internal Spotify API command
 * descriptor. Pure: no ESP-IDF, no I/O -- mirrors denon_intent.h's shape
 * exactly (domain check, kind switch, payload extraction), but the output
 * is a command descriptor for spotify_api.c to execute (Tier 2, target-
 * only), not a wire string -- Spotify's protocol is a set of distinct
 * HTTP endpoints, not a single text line format.
 *
 * No SEEK: NETWORKING.md §1.3's endpoint list omits `PUT .../seek`
 * entirely, and no scrub UI exists until Phase 5 -- cut explicitly, not
 * silently dropped (Phase 4 plan §5).
 *
 * playback_intent_kind_t/playback_intent_payload_t moved to core_types
 * (Phase 5 Tier 6: `ui` needs these exact values to submit PLAY/PAUSE/
 * NEXT/PREVIOUS and may not include this, a forbidden svc_* header) --
 * this file includes them as its own canonical definition, unchanged in
 * every other way.
 */
#ifndef SPOTIFY_INTENT_H
#define SPOTIFY_INTENT_H

#include <stdbool.h>
#include <stdint.h>

#include "intent_types.h"
#include "playback_intent_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What spotify_api.c (Tier 2) actually executes -- one command kind per
 * endpoint this project uses (NETWORKING.md §1.3's list). */
typedef enum {
    SPOTIFY_API_CMD_PLAY = 0,
    SPOTIFY_API_CMD_PAUSE,
    SPOTIFY_API_CMD_NEXT,
    SPOTIFY_API_CMD_PREVIOUS,
    SPOTIFY_API_CMD_SET_SHUFFLE,
    SPOTIFY_API_CMD_SET_REPEAT,
    SPOTIFY_API_CMD_SET_VOLUME,
} spotify_api_cmd_kind_t;

typedef struct {
    spotify_api_cmd_kind_t kind;
    union {
        bool    shuffle;        /* SET_SHUFFLE */
        uint8_t repeat_mode;    /* SET_REPEAT */
        uint8_t volume_pct;     /* SET_VOLUME -- already clamped to 0-100 */
    } u;
} spotify_api_cmd_t;

typedef enum {
    SPOTIFY_INTENT_RESULT_OK = 0,
    SPOTIFY_INTENT_RESULT_WRONG_DOMAIN,   /* intent->domain != INTENT_DOMAIN_PLAYBACK */
    SPOTIFY_INTENT_RESULT_UNKNOWN_KIND,
} spotify_intent_result_t;

spotify_intent_result_t spotify_intent_to_cmd(const intent_t *intent, spotify_api_cmd_t *out_cmd);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_INTENT_H */
