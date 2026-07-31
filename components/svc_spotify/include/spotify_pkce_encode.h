/*
 * base64url-no-pad encoding, PKCE's own required alphabet (RFC 7636 §4.2:
 * "Base64url Encoding" -- '+'/'/' replaced with '-'/'_', trailing '='
 * stripped entirely). Pure: no allocation, no ESP-IDF. Used for both the
 * PKCE `code_verifier` (encoding a random byte string) and `code_challenge`
 * (encoding its SHA-256 digest) -- the SHA-256 step itself is target-only
 * (mbedtls), this is the one genuinely pure sliver, host-tested with
 * vectors.
 */
#ifndef SPOTIFY_PKCE_ENCODE_H
#define SPOTIFY_PKCE_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* out must have room for at least spotify_base64url_encoded_len(data_len)
 * bytes including the NUL terminator. Returns the encoded string length
 * (excluding the NUL), or 0 if out_cap is too small. */
size_t spotify_base64url_encode(const uint8_t *data, size_t data_len, char *out, size_t out_cap);

/* Exact encoded length (excluding NUL) for a given input length -- no
 * padding, so this is ceil(data_len * 4 / 3) rounded down to whole
 * base64url digits per RFC 7636, not the padded base64 formula. */
size_t spotify_base64url_encoded_len(size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_PKCE_ENCODE_H */
