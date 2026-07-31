/*
 * Hand-rolled, strict parser for the flat manifest object described in
 * docs/PLATFORM.md §2.3. This runs on bytes fetched from an HTTPS host --
 * attacker-controlled if that host is compromised or spoofed -- so every
 * ambiguity is a reject, never a best-effort guess:
 *
 *   - duplicate keys                         -> reject
 *   - unrecognised keys                      -> reject (closed schema, not
 *                                                extensible-by-accident)
 *   - wrong JSON type for a known key        -> reject (e.g. `"build": "x"`)
 *   - negative / fractional / exponent
 *     numbers for integer fields             -> reject
 *   - leading-zero integers ("007")          -> reject (not valid JSON anyway)
 *   - integer overflow past uint32_t          -> reject
 *   - strings longer than the field's
 *     declared buffer                        -> reject, never truncated
 *   - unescaped control bytes in a string,
 *     including an embedded NUL              -> reject
 *   - `\u` escapes                           -> reject (unsupported by
 *       design -- every field we sign is ASCII-safe or raw UTF-8; the
 *       Python generator must emit ensure_ascii=False rather than \u, so
 *       both sides only ever need to agree on raw bytes, never on a
 *       surrogate-pair decoder)
 *   - `sha256` not exactly 64 hex chars       -> reject
 *   - `sig` not valid (padded) base64,
 *     or decoding past OTA_MANIFEST_SIG_MAX   -> reject
 *   - trailing bytes after the closing `}`    -> reject
 *   - any of the 8 mandatory fields absent    -> reject
 *
 * Only ever returns OTA_ADMIT_OK or OTA_REJECT_MALFORMED_MANIFEST -- see
 * ota_manifest.h.
 */
#include "ota_manifest.h"

#include "ota_manifest_codec_internal.h"

#include <string.h>

typedef struct {
    const char *s;
    size_t      len;
    size_t      pos;
} jcur_t;

static int peek(const jcur_t *c)
{
    if (c->pos >= c->len) {
        return -1;
    }
    return (unsigned char)c->s[c->pos];
}

static void adv(jcur_t *c)
{
    if (c->pos < c->len) {
        c->pos++;
    }
}

static void skip_ws(jcur_t *c)
{
    for (;;) {
        int ch = peek(c);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            adv(c);
        } else {
            break;
        }
    }
}

static bool require_char(jcur_t *c, char want)
{
    if (peek(c) != (unsigned char)want) {
        return false;
    }
    adv(c);
    return true;
}

static bool is_ident_char(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

/* out_cap is the FULL destination buffer size (room for the NUL included).
 * Always NUL-terminates on success. */
static bool parse_string_field(jcur_t *c, char *out, size_t out_cap, size_t *out_len)
{
    if (out_cap < 1 || !require_char(c, '"')) {
        return false;
    }

    size_t n = 0;
    for (;;) {
        int ch = peek(c);
        if (ch < 0) {
            return false;   /* unterminated string */
        }
        if (ch == '"') {
            adv(c);
            break;
        }

        char decoded;
        if (ch == '\\') {
            adv(c);
            int esc = peek(c);
            switch (esc) {
            case '"':  decoded = '"';  break;
            case '\\': decoded = '\\'; break;
            case '/':  decoded = '/';  break;
            case 'b':  decoded = '\b'; break;
            case 'f':  decoded = '\f'; break;
            case 'n':  decoded = '\n'; break;
            case 'r':  decoded = '\r'; break;
            case 't':  decoded = '\t'; break;
            default:   return false;   /* incl. \u and a trailing backslash (esc == -1) */
            }
            adv(c);
        } else if (ch < 0x20) {
            return false;   /* raw control byte, including an embedded NUL */
        } else {
            decoded = (char)ch;
            adv(c);
        }

        if (n >= out_cap - 1) {
            return false;   /* overlong string */
        }
        out[n++] = decoded;
    }

    out[n] = '\0';
    *out_len = n;
    return true;
}

static bool parse_uint32(jcur_t *c, uint32_t *out)
{
    int ch = peek(c);
    if (ch < '0' || ch > '9') {
        return false;   /* also rejects a leading '-': no negative field is valid here */
    }

    uint64_t acc = 0;
    if (ch == '0') {
        adv(c);
        int next = peek(c);
        if (next >= '0' && next <= '9') {
            return false;   /* "007" -- not valid JSON, and not ambiguous either way */
        }
    } else {
        while ((ch = peek(c)) >= '0' && ch <= '9') {
            acc = acc * 10 + (uint32_t)(ch - '0');
            if (acc > 0xFFFFFFFFu) {
                return false;   /* overflow past uint32_t */
            }
            adv(c);
        }
    }

    int after = peek(c);
    if (after == '.' || after == 'e' || after == 'E') {
        return false;   /* fraction/exponent -- every field we parse is a plain integer */
    }

    *out = (uint32_t)acc;
    return true;
}

static bool parse_bool(jcur_t *c, bool *out)
{
    if (c->pos + 4 <= c->len && memcmp(&c->s[c->pos], "true", 4) == 0 &&
        !is_ident_char(c->pos + 4 < c->len ? (unsigned char)c->s[c->pos + 4] : -1)) {
        c->pos += 4;
        *out = true;
        return true;
    }
    if (c->pos + 5 <= c->len && memcmp(&c->s[c->pos], "false", 5) == 0 &&
        !is_ident_char(c->pos + 5 < c->len ? (unsigned char)c->s[c->pos + 5] : -1)) {
        c->pos += 5;
        *out = false;
        return true;
    }
    return false;
}

typedef enum {
    FIELD_VERSION = 0,
    FIELD_BUILD,
    FIELD_SECURITY_VERSION,
    FIELD_URL,
    FIELD_SHA256,
    FIELD_SIZE,
    FIELD_SIG,
    FIELD_KEY_ID,
    FIELD_MIN_PREV_BUILD,
    FIELD_MANDATORY,
    FIELD_NOTES,
    FIELD_COUNT,
} field_id_t;

static bool field_id_for_key(const char *key, field_id_t *out)
{
    static const struct { const char *name; field_id_t id; } TABLE[] = {
        { "version",          FIELD_VERSION },
        { "build",            FIELD_BUILD },
        { "security_version", FIELD_SECURITY_VERSION },
        { "url",              FIELD_URL },
        { "sha256",           FIELD_SHA256 },
        { "size",             FIELD_SIZE },
        { "sig",              FIELD_SIG },
        { "key_id",           FIELD_KEY_ID },
        { "min_prev_build",   FIELD_MIN_PREV_BUILD },
        { "mandatory",        FIELD_MANDATORY },
        { "notes",            FIELD_NOTES },
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (strcmp(key, TABLE[i].name) == 0) {
            *out = TABLE[i].id;
            return true;
        }
    }
    return false;
}

ota_admit_t ota_manifest_parse_json(const char *json, size_t json_len, ota_manifest_full_t *out)
{
    memset(out, 0, sizeof(*out));

    jcur_t c = { .s = json, .len = json_len, .pos = 0 };
    bool seen[FIELD_COUNT] = { false };
    char sha_hex[65];
    size_t sha_hex_len = 0;
    char sig_b64[112];
    size_t sig_b64_len = 0;

    skip_ws(&c);
    if (!require_char(&c, '{')) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }
    skip_ws(&c);

    if (peek(&c) != '}') {
        for (;;) {
            char key[32];
            size_t key_len;
            if (!parse_string_field(&c, key, sizeof(key), &key_len)) {
                return OTA_REJECT_MALFORMED_MANIFEST;
            }

            skip_ws(&c);
            if (!require_char(&c, ':')) {
                return OTA_REJECT_MALFORMED_MANIFEST;
            }
            skip_ws(&c);

            field_id_t fid;
            if (!field_id_for_key(key, &fid) || seen[fid]) {
                return OTA_REJECT_MALFORMED_MANIFEST;   /* unknown or duplicate key */
            }

            size_t discard;
            switch (fid) {
            case FIELD_VERSION:
                if (!parse_string_field(&c, out->version, sizeof(out->version), &discard)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_URL:
                if (!parse_string_field(&c, out->url, sizeof(out->url), &discard)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_KEY_ID:
                if (!parse_string_field(&c, out->key_id, sizeof(out->key_id), &discard)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_NOTES:
                if (!parse_string_field(&c, out->notes, sizeof(out->notes), &discard)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_SHA256:
                if (!parse_string_field(&c, sha_hex, sizeof(sha_hex), &sha_hex_len) || sha_hex_len != 64) {
                    return OTA_REJECT_MALFORMED_MANIFEST;
                }
                break;
            case FIELD_SIG:
                if (!parse_string_field(&c, sig_b64, sizeof(sig_b64), &sig_b64_len)) {
                    return OTA_REJECT_MALFORMED_MANIFEST;
                }
                break;
            case FIELD_BUILD:
                if (!parse_uint32(&c, &out->build)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_SECURITY_VERSION:
                if (!parse_uint32(&c, &out->security_version)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_SIZE:
                if (!parse_uint32(&c, &out->size)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_MIN_PREV_BUILD:
                if (!parse_uint32(&c, &out->min_prev_build)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            case FIELD_MANDATORY:
                if (!parse_bool(&c, &out->mandatory)) return OTA_REJECT_MALFORMED_MANIFEST;
                break;
            default:
                return OTA_REJECT_MALFORMED_MANIFEST;
            }
            seen[fid] = true;

            skip_ws(&c);
            int nx = peek(&c);
            if (nx == ',') {
                adv(&c);
                skip_ws(&c);
                continue;
            }
            if (nx == '}') {
                break;
            }
            return OTA_REJECT_MALFORMED_MANIFEST;
        }
    }

    if (!require_char(&c, '}')) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }
    skip_ws(&c);
    if (c.pos != c.len) {
        return OTA_REJECT_MALFORMED_MANIFEST;   /* trailing bytes after the object */
    }

    static const field_id_t MANDATORY_FIELDS[] = {
        FIELD_VERSION, FIELD_BUILD, FIELD_SECURITY_VERSION, FIELD_URL,
        FIELD_SHA256, FIELD_SIZE, FIELD_SIG, FIELD_KEY_ID,
    };
    for (size_t i = 0; i < sizeof(MANDATORY_FIELDS) / sizeof(MANDATORY_FIELDS[0]); i++) {
        if (!seen[MANDATORY_FIELDS[i]]) {
            return OTA_REJECT_MALFORMED_MANIFEST;
        }
    }

    if (!ota_manifest_hex_decode32(sha_hex, out->sha256)) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }
    if (!ota_manifest_base64_decode(sig_b64, sig_b64_len, out->sig, sizeof(out->sig), &out->sig_len)) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }

    return OTA_ADMIT_OK;
}
