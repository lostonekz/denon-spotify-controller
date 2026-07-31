/*
 * Builds the Spotify OAuth authorize URL `spotify pair` prints for the user
 * to open. Pure string formatting -- no network, no crypto (the PKCE
 * verifier/challenge/state values are generated elsewhere and passed in
 * already-computed). Host-tested against a fixed set of inputs.
 *
 * Carries the OAuth `scope` parameter explicitly: without
 * `user-read-playback-state user-modify-playback-state`, Spotify grants
 * only public-profile access -- pairing would visibly succeed (a valid
 * token comes back) while every real `/v1/me/player` call afterward
 * returned 403, a confusing working-looking dead end.
 */
#ifndef SPOTIFY_AUTHORIZE_URL_H
#define SPOTIFY_AUTHORIZE_URL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *client_id;
    const char *redirect_uri;     /* percent-encoded by this module */
    const char *code_challenge;   /* base64url, already safe -- not re-encoded */
    const char *state;            /* hex/base64url, already safe -- not re-encoded */
} spotify_authorize_url_params_t;

/* Returns the built URL's length (excluding NUL), or 0 if out_cap is too
 * small or any required param is NULL/empty. */
size_t spotify_authorize_url_build(const spotify_authorize_url_params_t *params,
                                     char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_AUTHORIZE_URL_H */
