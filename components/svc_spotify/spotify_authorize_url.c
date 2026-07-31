#include "spotify_authorize_url.h"

#include <stdbool.h>
#include <stdio.h>

#define SPOTIFY_AUTHORIZE_BASE_URL "https://accounts.spotify.com/authorize"

/* Fixed, not user-configurable -- the one thing an earlier draft of this
 * plan forgot entirely. Without it, Spotify grants only public-profile
 * access: pairing succeeds (a valid token comes back) while every real
 * /v1/me/player call afterward returns 403. */
#define SPOTIFY_AUTHORIZE_SCOPE_ENCODED "user-read-playback-state%20user-modify-playback-state"

static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

/* RFC 3986 percent-encoding for a query-string value. Only redirect_uri
 * needs this: client_id is a fixed-format hex string, code_challenge/state
 * are already base64url/hex (self-safe alphabets). */
static size_t percent_encode(const char *in, char *out, size_t out_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;

    for (size_t i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        if (is_unreserved((char)c)) {
            if (o + 1 >= out_cap) return 0;
            out[o++] = (char)c;
        } else {
            if (o + 3 >= out_cap) return 0;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0Fu];
        }
    }
    if (o >= out_cap) return 0;
    out[o] = '\0';
    return o;
}

size_t spotify_authorize_url_build(const spotify_authorize_url_params_t *params,
                                     char *out, size_t out_cap)
{
    if (params == NULL || out == NULL ||
        params->client_id == NULL      || params->client_id[0] == '\0' ||
        params->redirect_uri == NULL   || params->redirect_uri[0] == '\0' ||
        params->code_challenge == NULL || params->code_challenge[0] == '\0' ||
        params->state == NULL          || params->state[0] == '\0') {
        return 0;
    }

    char redirect_encoded[256];
    if (percent_encode(params->redirect_uri, redirect_encoded, sizeof(redirect_encoded)) == 0) {
        return 0;
    }

    int n = snprintf(out, out_cap,
        "%s?client_id=%s&response_type=code&redirect_uri=%s"
        "&code_challenge_method=S256&code_challenge=%s&state=%s&scope=%s",
        SPOTIFY_AUTHORIZE_BASE_URL, params->client_id, redirect_encoded,
        params->code_challenge, params->state, SPOTIFY_AUTHORIZE_SCOPE_ENCODED);

    if (n < 0 || (size_t)n >= out_cap) {
        return 0;
    }
    return (size_t)n;
}
