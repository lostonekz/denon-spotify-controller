#include "view_model_text.h"

#include <stdint.h>
#include <string.h>

#define VM_REPLACEMENT_UTF8 "\xEF\xBF\xBD"   /* U+FFFD, 3 bytes */
#define VM_REPLACEMENT_LEN  3u

/* Returns the byte length (1-4) of the well-formed UTF-8 sequence starting
 * at s[0], given `remaining` bytes available from s[0] onward, or 0 if
 * s[0] does not begin a well-formed sequence that fits within `remaining`
 * (a stray continuation byte, an invalid leading byte, a truncated
 * continuation, an overlong encoding, a UTF-16 surrogate, or a codepoint
 * above U+10FFFF -- every class of "well-formed bytes, garbage codepoint"
 * this decoder must reject, not just malformed byte patterns). */
static size_t utf8_seq_len(const unsigned char *s, size_t remaining)
{
    unsigned char b0 = s[0];

    if (b0 < 0x80u) {
        return 1;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (remaining < 2 || (s[1] & 0xC0u) != 0x80u) {
            return 0;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        return (cp < 0x80u) ? 0u : 2u;   /* < 0x80 is an overlong encoding */
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (remaining < 3 || (s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u) {
            return 0;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
                       (uint32_t)(s[2] & 0x3Fu);
        if (cp < 0x800u) {
            return 0;   /* overlong */
        }
        if (cp >= 0xD800u && cp <= 0xDFFFu) {
            return 0;   /* UTF-16 surrogate -- not a valid UTF-8 codepoint */
        }
        return 3;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (remaining < 4 || (s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u ||
            (s[3] & 0xC0u) != 0x80u) {
            return 0;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
                       ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu) {
            return 0;   /* overlong, or above the Unicode range */
        }
        return 4;
    }
    return 0;   /* stray continuation byte (0x80-0xBF) or an invalid leading
                  * byte (0xF8-0xFF, or the reserved 0xC0/0xC1 already caught
                  * by the overlong check above) */
}

size_t view_model_text_copy_safe(char *dst, size_t dst_cap, const char *src)
{
    if (dst_cap == 0) {
        return 0;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 0;
    }

    size_t src_len = strlen(src);
    const unsigned char *s = (const unsigned char *)src;
    size_t si = 0;
    size_t di = 0;
    size_t budget = dst_cap - 1;   /* leave room for the NUL */

    while (si < src_len && di < budget) {
        size_t remaining = src_len - si;
        size_t seq_len = utf8_seq_len(s + si, remaining);

        if (seq_len == 0) {
            if (di + VM_REPLACEMENT_LEN > budget) {
                break;   /* not even the whole replacement character fits -- stop, don't split it */
            }
            memcpy(dst + di, VM_REPLACEMENT_UTF8, VM_REPLACEMENT_LEN);
            di += VM_REPLACEMENT_LEN;
            si += 1;   /* advance past just the one bad byte */
            continue;
        }

        if (di + seq_len > budget) {
            break;   /* the whole codepoint doesn't fit -- truncate here, never split it */
        }
        memcpy(dst + di, s + si, seq_len);
        di += seq_len;
        si += seq_len;
    }

    dst[di] = '\0';
    return di;
}

bool view_model_text_has_content(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }

    size_t len = strlen(s);
    size_t i = 0;
    while (i < len) {
        if (len - i >= VM_REPLACEMENT_LEN &&
            memcmp(s + i, VM_REPLACEMENT_UTF8, VM_REPLACEMENT_LEN) == 0) {
            i += VM_REPLACEMENT_LEN;
            continue;
        }
        return true;   /* a byte that isn't part of a U+FFFD sequence */
    }
    return false;   /* empty, or entirely U+FFFD */
}

size_t view_model_text_count_replacements(const char *s)
{
    if (s == NULL) {
        return 0;
    }

    size_t len = strlen(s);
    size_t count = 0;
    size_t i = 0;
    while (i + VM_REPLACEMENT_LEN <= len) {
        if (memcmp(s + i, VM_REPLACEMENT_UTF8, VM_REPLACEMENT_LEN) == 0) {
            count++;
            i += VM_REPLACEMENT_LEN;
        } else {
            i++;
        }
    }
    return count;
}
