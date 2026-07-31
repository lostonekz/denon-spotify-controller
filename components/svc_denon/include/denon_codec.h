/*
 * Denon master-volume wire codec.
 *
 * Pure functions, no ESP-IDF dependency, no allocation, no I/O. This file is
 * host-testable and IS host-tested (test/host/test_denon_codec.c).
 *
 * WHY THIS IS ITS OWN FILE WITH ITS OWN TEST TABLE:
 * The Denon MV encoding is ambiguous by design. "MV52" is 52.0 dB but "MV525"
 * is 52.5 dB, not 525. A parser that gets this wrong produces a 10 dB error
 * into real speakers. See docs/RISKS.md R13.
 *
 * Representation is HALF-DECIBEL STEPS as an integer. No floats anywhere near
 * the volume path: 52.5 dB == 105. This makes every boundary exact and every
 * comparison trivially correct.
 */
#ifndef DENON_CODEC_H
#define DENON_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Denon absolute volume scale is 0.0 .. 98.0 dB in 0.5 steps.
 * 98.0 dB == 196 half-steps. 98.5 exists on the wire on some models. */
#define DENON_MV_HALF_DB_MIN   0u
#define DENON_MV_HALF_DB_MAX   197u   /* 98.5 dB */

/* Longest output is "MV985" + NUL. */
#define DENON_MV_BUF_LEN       8u

typedef enum {
    DENON_CODEC_OK = 0,

    /* Not an MV command at all (e.g. "MUON", "PWON"). Caller should try
     * another decoder. This is NOT an error. */
    DENON_CODEC_NOT_MV,

    /* Looks like MV but the payload is unparseable: wrong digit count,
     * non-digits, empty. */
    DENON_CODEC_MALFORMED,

    /* Parsed cleanly but outside the representable/safe range. */
    DENON_CODEC_OUT_OF_RANGE,

    /* "MVMAX 98" — a max-volume REPORT, not a level.
     *
     * This is the single nastiest case in the protocol. A naive parser doing
     * atoi(s + 2) on "MVMAX 98" yields 0 and silently sets the volume to
     * minimum -- or, with a different naive parser, to 98. Both are wrong and
     * one of them is loud. It gets its own status so a caller cannot
     * accidentally treat it as a level. */
    DENON_CODEC_MAX_REPORT,
} denon_codec_status_t;

/*
 * Parse a Denon MV wire string.
 *
 * @param s        NUL-terminated wire payload, trailing \r already stripped.
 * @param out      Receives half-dB value on DENON_CODEC_OK.
 *                 On DENON_CODEC_MAX_REPORT receives the reported maximum.
 *                 Untouched on any other status.
 * @return status, see enum.
 *
 * Never reads past the NUL. Never writes *out unless returning OK or
 * MAX_REPORT.
 */
denon_codec_status_t denon_mv_parse(const char *s, uint16_t *out);

/*
 * Format a half-dB value as a Denon MV wire string (no trailing \r).
 *
 * Zero-padding is mandatory: 5.0 dB is "MV05", never "MV5". An unpadded value
 * is silently ignored or misparsed by the receiver.
 *
 * @param half_db  0 .. DENON_MV_HALF_DB_MAX
 * @param buf      Output, at least DENON_MV_BUF_LEN bytes.
 * @param buflen   sizeof(buf).
 * @return DENON_CODEC_OK, or OUT_OF_RANGE, or MALFORMED if buf is too small.
 */
denon_codec_status_t denon_mv_format(uint16_t half_db, char *buf, size_t buflen);

/* Convenience for tests and UI formatting: 105 -> "52.5". Not on the hot path. */
void denon_mv_to_display(uint16_t half_db, char *buf, size_t buflen);

/*
 * The volume safety clamp (docs/adr/0007, docs/PLATFORM.md §3.4, docs/RISKS.md
 * R13 -- the same requirement stated three independent times). A stray touch
 * or a coalescing bug must never be able to send an outgoing MV command above
 * the user's configured limit, regardless of which screen, gesture, or future
 * integration issued the request. Called at the point an outgoing MV command
 * is formed, inside svc_denon -- never relied on in the UI layer.
 *
 * Also clamps to DENON_MV_HALF_DB_MAX regardless of limit_half_db, so an
 * out-of-range limit (a settings-storage bug, not user error) can't itself
 * become the hole this function exists to close.
 *
 * Pure min(); a real function (not a macro) so it has one obvious call site
 * to audit and one boundary test table, matching R13's explicit test intent.
 */
uint16_t denon_clamp_volume(uint16_t requested_half_db, uint16_t limit_half_db);

#ifdef __cplusplus
}
#endif

#endif /* DENON_CODEC_H */
