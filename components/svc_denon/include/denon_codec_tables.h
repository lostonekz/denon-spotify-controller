/*
 * Denon PW (power) / MU (mute) / SI (input select) / MS (listening mode)
 * wire codecs. Split out from denon_codec.h/.c (which stays MV-only) per
 * docs/ROADMAP.md Phase 3's plan: MV alone was already 151/93 lines of
 * genuinely MV-specific half-step ambiguity handling; adding four more
 * command families to that file risked the 400-line gate for no benefit --
 * none of these four have MV's numeric-encoding ambiguity, so they don't
 * belong in the file whose whole reason to exist is that ambiguity.
 *
 * Pure functions, no ESP-IDF dependency, no allocation, no I/O. Host-tested
 * (test/host/test_denon_codec_tables.c).
 *
 * denon_input_id_t / denon_listening_mode_id_t are the STABLE ORDINALS
 * denon_state_t.input_id / .listening_mode_id store -- never the wire
 * string. This file owns the ordinal<->wire-code mapping so nothing else
 * in the codebase needs to know Denon's actual command text.
 *
 * KNOWN GAP, flagged rather than hidden: PW/MU are stable, universal Denon/
 * Marantz commands unchanged across a decade-plus of models -- high
 * confidence. SI/MU is used loosely there; SI/MS are lower confidence.
 * The SI (input) and MS (listening mode) tables below are a conservative
 * core subset of well-established codes, NOT transcribed from a captured
 * AVR-X3800H session or Denon's own IP Protocol document (neither was
 * available while writing this). docs/RISKS.md R5's mitigation ("a Denon
 * capability probe at startup detects a changed device rather than
 * misbehaving") does NOT cover "this table is incomplete or a code is
 * wrong" -- that needs real-hardware verification against the actual unit
 * before Tier 6 is considered done, and the table is intentionally easy to
 * extend (one row per code) once verified.
 */
#ifndef DENON_CODEC_TABLES_H
#define DENON_CODEC_TABLES_H

#include <stddef.h>
#include <stdint.h>

#include "denon_intent_types.h"   /* denon_input_id_t's canonical definition --
                                      moved to core_types so `ui`'s w_power_glyph
                                      can construct it too; see that header's own
                                      comment */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DENON_TABLE_OK = 0,
    DENON_TABLE_NOT_MATCHED,   /* input isn't this command family at all -- try another */
    DENON_TABLE_MALFORMED,     /* looks like this family but the payload doesn't match any known code */
} denon_table_status_t;

/* Longest wire string this file emits, +1 NUL. "MSDOLBY DIGITAL" is 15
 * chars; round up for headroom without guessing a smaller exact bound. */
#define DENON_TABLE_BUF_LEN 24u

/* --- PW: power --- */

typedef enum {
    DENON_PW_ON = 0,
    DENON_PW_STANDBY,
} denon_power_t;

denon_table_status_t denon_pw_parse(const char *s, denon_power_t *out);
denon_table_status_t denon_pw_format(denon_power_t v, char *buf, size_t buflen);

/* --- MU: mute --- */

typedef enum {
    DENON_MU_ON = 0,
    DENON_MU_OFF,
} denon_mute_t;

denon_table_status_t denon_mu_parse(const char *s, denon_mute_t *out);
denon_table_status_t denon_mu_format(denon_mute_t v, char *buf, size_t buflen);

/* --- SI: input select (see the KNOWN GAP note above) --- */
/* denon_input_id_t's canonical definition now lives in denon_intent_types.h
 * (core_types) -- included above. */

denon_table_status_t denon_si_parse(const char *s, denon_input_id_t *out);
denon_table_status_t denon_si_format(denon_input_id_t v, char *buf, size_t buflen);

/* --- MS: listening/surround mode (see the KNOWN GAP note above) --- */

typedef enum {
    DENON_MODE_MOVIE = 0,
    DENON_MODE_MUSIC,
    DENON_MODE_GAME,
    DENON_MODE_DIRECT,
    DENON_MODE_PURE_DIRECT,
    DENON_MODE_STEREO,
    DENON_MODE_STANDARD,
    DENON_MODE_COUNT,   /* not a real mode -- array-sizing sentinel */
} denon_listening_mode_id_t;

denon_table_status_t denon_ms_parse(const char *s, denon_listening_mode_id_t *out);
denon_table_status_t denon_ms_format(denon_listening_mode_id_t v, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* DENON_CODEC_TABLES_H */
