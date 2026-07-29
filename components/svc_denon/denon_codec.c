/*
 * Denon master-volume wire codec. See denon_codec.h for why this is isolated.
 *
 * No allocation, no I/O, no ESP-IDF. Host-tested.
 */

#include "denon_codec.h"

#include <string.h>

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* "MVMAX 98" / "MVMAX 985" — a maximum-volume report, never a level. */
static const char MV_MAX_PREFIX[] = "MVMAX";

/* Internal: parse the bare digit payload. Not public API — callers go through
 * denon_mv_parse() so the MVMAX case can never be bypassed. */
static denon_codec_status_t parse_digits(const char *p, uint16_t *out);

denon_codec_status_t denon_mv_parse(const char *s, uint16_t *out)
{
    if (s == NULL || out == NULL) {
        return DENON_CODEC_MALFORMED;
    }

    if (s[0] != 'M' || s[1] != 'V') {
        return DENON_CODEC_NOT_MV;
    }

    /* Check MVMAX before digit parsing. Order matters: "MVMAX 98" starts with
     * "MV" and would otherwise fall into the digit path and parse as garbage. */
    if (strncmp(s, MV_MAX_PREFIX, sizeof(MV_MAX_PREFIX) - 1) == 0) {
        const char *p = s + sizeof(MV_MAX_PREFIX) - 1;

        while (*p == ' ') {
            p++;
        }

        uint16_t max_half_db = 0;
        denon_codec_status_t st = parse_digits(p, &max_half_db);
        if (st != DENON_CODEC_OK) {
            return DENON_CODEC_MALFORMED;
        }

        *out = max_half_db;
        return DENON_CODEC_MAX_REPORT;
    }

    return parse_digits(s + 2, out);
}

/*
 * Parse the bare digit payload: "52" -> 104, "525" -> 105.
 *
 * Two digits are whole dB. Three digits are whole dB plus a half step, where
 * the third digit is 0 or 5 -- NOT a units digit. "525" is 52.5, not 525.
 */
static denon_codec_status_t parse_digits(const char *p, uint16_t *out)
{
    size_t n = 0;

    while (p[n] != '\0') {
        if (!is_digit(p[n])) {
            return DENON_CODEC_MALFORMED;
        }
        n++;
        if (n > 3) {
            return DENON_CODEC_MALFORMED; /* 4+ digits is not a thing */
        }
    }

    if (n != 2 && n != 3) {
        return DENON_CODEC_MALFORMED; /* 0 or 1 digit is malformed */
    }

    uint16_t whole = (uint16_t)((p[0] - '0') * 10 + (p[1] - '0'));
    uint16_t half  = 0;

    if (n == 3) {
        char third = p[2];
        if (third == '5') {
            half = 1;
        } else if (third == '0') {
            half = 0;
        } else {
            /* Denon only ever emits 0 or 5 in the half position. Anything
             * else means we have misread the frame; refusing is safer than
             * guessing at a volume. */
            return DENON_CODEC_MALFORMED;
        }
    }

    uint32_t half_db = (uint32_t)whole * 2u + half;

    if (half_db > DENON_MV_HALF_DB_MAX) {
        return DENON_CODEC_OUT_OF_RANGE;
    }

    *out = (uint16_t)half_db;
    return DENON_CODEC_OK;
}

denon_codec_status_t denon_mv_format(uint16_t half_db, char *buf, size_t buflen)
{
    if (buf == NULL || buflen < DENON_MV_BUF_LEN) {
        return DENON_CODEC_MALFORMED;
    }
    if (half_db > DENON_MV_HALF_DB_MAX) {
        return DENON_CODEC_OUT_OF_RANGE;
    }

    uint16_t whole = half_db / 2u;
    bool     half  = (half_db % 2u) != 0u;

    /* Zero-padding is mandatory. "MV5" is silently dropped by the receiver. */
    buf[0] = 'M';
    buf[1] = 'V';
    buf[2] = (char)('0' + (whole / 10u));
    buf[3] = (char)('0' + (whole % 10u));

    if (half) {
        buf[4] = '5';
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }

    return DENON_CODEC_OK;
}

void denon_mv_to_display(uint16_t half_db, char *buf, size_t buflen)
{
    if (buf == NULL || buflen < 6) {
        return;
    }

    uint16_t whole = half_db / 2u;
    bool     half  = (half_db % 2u) != 0u;

    size_t i = 0;
    if (whole >= 10u) {
        buf[i++] = (char)('0' + (whole / 10u));
    }
    buf[i++] = (char)('0' + (whole % 10u));
    buf[i++] = '.';
    buf[i++] = half ? '5' : '0';
    buf[i]   = '\0';
}
