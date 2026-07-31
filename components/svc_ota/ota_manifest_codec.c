#include "ota_manifest_codec_internal.h"

#include <string.h>

static int hex_val(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool ota_manifest_hex_decode32(const char *hex, uint8_t *out)
{
    for (int i = 0; i < 32; i++) {
        int hi = hex_val((unsigned char)hex[i * 2]);
        int lo = hex_val((unsigned char)hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static int b64_val(int ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

bool ota_manifest_base64_decode(const char *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (in_len == 0 || in_len % 4 != 0) {
        return false;
    }

    size_t pad = 0;
    if (in[in_len - 1] == '=') pad++;
    if (in[in_len - 2] == '=') pad++;
    for (size_t i = 0; i < in_len - pad; i++) {
        if (in[i] == '=') {
            return false;   /* '=' outside the final 1-2 positions */
        }
    }

    size_t n = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        bool last_group = (i + 4 == in_len);
        int c0 = b64_val((unsigned char)in[i]);
        int c1 = b64_val((unsigned char)in[i + 1]);
        int c2 = (last_group && pad >= 2) ? 0 : b64_val((unsigned char)in[i + 2]);
        int c3 = (last_group && pad >= 1) ? 0 : b64_val((unsigned char)in[i + 3]);
        if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) {
            return false;
        }

        uint32_t triple = ((uint32_t)c0 << 18) | ((uint32_t)c1 << 12) | ((uint32_t)c2 << 6) | (uint32_t)c3;
        uint8_t bytes[3] = {
            (uint8_t)((triple >> 16) & 0xFF),
            (uint8_t)((triple >> 8) & 0xFF),
            (uint8_t)(triple & 0xFF),
        };
        size_t nbytes = 3;
        if (last_group) {
            if (pad == 1) nbytes = 2;
            else if (pad == 2) nbytes = 1;
        }

        if (n + nbytes > out_cap) {
            return false;
        }
        memcpy(&out[n], bytes, nbytes);
        n += nbytes;
    }

    *out_len = n;
    return true;
}
