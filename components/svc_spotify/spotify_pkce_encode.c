#include "spotify_pkce_encode.h"

static const char B64URL_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

size_t spotify_base64url_encoded_len(size_t data_len)
{
    size_t full_groups = data_len / 3;
    size_t remainder   = data_len % 3;
    size_t len = full_groups * 4;
    if (remainder == 1) {
        len += 2;
    } else if (remainder == 2) {
        len += 3;
    }
    return len;
}

size_t spotify_base64url_encode(const uint8_t *data, size_t data_len, char *out, size_t out_cap)
{
    size_t needed = spotify_base64url_encoded_len(data_len);
    if (out == NULL || out_cap < needed + 1) {
        return 0;
    }
    if (data == NULL && data_len > 0) {
        return 0;
    }

    size_t i = 0, o = 0;
    while (i + 3 <= data_len) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
        out[o++] = B64URL_ALPHABET[(n >> 18) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[(n >> 12) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[(n >> 6) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[n & 0x3Fu];
        i += 3;
    }

    size_t remainder = data_len - i;
    if (remainder == 1) {
        uint32_t n = (uint32_t)data[i] << 16;
        out[o++] = B64URL_ALPHABET[(n >> 18) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[(n >> 12) & 0x3Fu];
    } else if (remainder == 2) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[o++] = B64URL_ALPHABET[(n >> 18) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[(n >> 12) & 0x3Fu];
        out[o++] = B64URL_ALPHABET[(n >> 6) & 0x3Fu];
    }

    out[o] = '\0';
    return o;
}
