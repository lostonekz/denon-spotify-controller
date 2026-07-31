/*
 * Spotify JSON -> core_types / internal structs. The ONLY file that knows
 * Spotify's vendor field names -- docs/RISKS.md R5's drift mitigation. Uses
 * cJSON (ESP-IDF's built-in `json` component), a deliberate deviation from
 * svc_ota's hand-rolled-parser precedent: a `/v1/me/player` response is a
 * large, evolving, third-party JSON blob where R5 wants tolerance to
 * unknown/additive fields, not svc_ota's closed-schema strict rejection.
 * Unknown fields are ignored; missing optional fields leave their output
 * untouched (caller-supplied defaults / zero-init stand).
 *
 * Pure except for the cJSON allocation itself, which is bounded to one
 * parse call's lifetime (the tree is built and freed within a single
 * function call, never retained). Fixture-driven host tests: see
 * test/host/test_spotify_parse.c and its JSON fixtures under
 * test/host/fixtures.
 */
#ifndef SPOTIFY_PARSE_H
#define SPOTIFY_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state_types.h"      /* playback_state_t and its field-size macros */
#include "spotify_credentials.h"  /* SPOTIFY_REFRESH_TOKEN_MAX */

#ifdef __cplusplus
extern "C" {
#endif

/* playback_state_t.repeat_mode's representation -- defined here since this
 * is the one file that produces it, same precedent as denon_codec_tables.h
 * defining denon_state_t.input_id's representation. */
typedef enum {
    SPOTIFY_REPEAT_OFF = 0,
    SPOTIFY_REPEAT_TRACK,
    SPOTIFY_REPEAT_CONTEXT,
} spotify_repeat_mode_t;

typedef enum {
    SPOTIFY_PARSE_OK = 0,
    /* A real, valid response meaning "nothing is playing anywhere" (204 or
     * an empty body) -- a distinct non-error, not a parse failure. */
    SPOTIFY_PARSE_NO_ACTIVE_DEVICE,
    SPOTIFY_PARSE_MALFORMED,
} spotify_parse_status_t;

/* GET /v1/me/player -> the playback_state_t fields this file can fill in.
 * Does NOT set .meta or .device_is_avr -- .meta is core_state's job, and
 * device_is_avr is spotify_avr_match's job (done by svc_spotify with the
 * pinned config this file has no access to). Untouched fields (album art
 * missing, no album object, etc.) are left at whatever the caller
 * initialized the struct to -- callers should zero it first. */
spotify_parse_status_t spotify_parse_player_state(const char *json, size_t json_len,
                                                     playback_state_t *out);

#define SPOTIFY_PARSE_MAX_DEVICES 10
#define SPOTIFY_PARSE_DEVICE_TYPE_MAX 32

typedef struct {
    char device_id[PLAYBACK_DEVICE_ID_MAX];
    char device_name[PLAYBACK_DEVICE_NAME_MAX];
    char device_type[SPOTIFY_PARSE_DEVICE_TYPE_MAX];   /* diagnostic only, never decides anything */
    bool supports_volume;
    bool is_restricted;
} spotify_device_entry_t;

typedef struct {
    spotify_device_entry_t devices[SPOTIFY_PARSE_MAX_DEVICES];
    size_t                 count;
} spotify_device_list_t;

/* GET /v1/me/player/devices -> a full device listing, for
 * spotify_avr_match.h's disambiguation. */
spotify_parse_status_t spotify_parse_device_list(const char *json, size_t json_len,
                                                    spotify_device_list_t *out);

#define SPOTIFY_ACCESS_TOKEN_MAX 300

typedef enum {
    SPOTIFY_TOKEN_RESULT_OK = 0,
    /* "error":"invalid_grant" -- a genuinely dead refresh token, per §2's
     * FSM design the ONLY case that goes straight to REAUTH_REQUIRED. */
    SPOTIFY_TOKEN_RESULT_INVALID_GRANT,
    /* Some other "error" field, or a non-200 response with no parseable
     * body -- treated as transient/retryable by the caller. */
    SPOTIFY_TOKEN_RESULT_OTHER_ERROR,
    SPOTIFY_TOKEN_RESULT_MALFORMED,
} spotify_token_parse_result_t;

typedef struct {
    char     access_token[SPOTIFY_ACCESS_TOKEN_MAX];
    char     refresh_token[SPOTIFY_REFRESH_TOKEN_MAX];   /* may be empty -- rotation isn't every response */
    uint32_t expires_in_s;
} spotify_token_response_t;

/* POST accounts.spotify.com/api/token's response body (exchange or
 * refresh -- same shape either way). */
spotify_token_parse_result_t spotify_parse_token_response(const char *json, size_t json_len,
                                                              spotify_token_response_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_PARSE_H */
