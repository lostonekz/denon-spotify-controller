/*
 * PKCE verifier/challenge/state generation, the authorize-URL build for
 * `spotify pair` to print, and the two accounts.spotify.com/api/token
 * calls (exchange, refresh). Target-only: the SHA-256 step (mbedtls) and
 * random-byte generation (esp_random) have no host-test path in this
 * codebase (mbedTLS is linked into zero host tests today -- ota_verify.c,
 * the only other consumer, is target-only too). The one genuinely pure
 * sliver is split into spotify_pkce_encode.c/spotify_authorize_url.c,
 * already host-tested; this file is verified by idf.py build + Tier 6
 * hardware only.
 *
 * The pure types (spotify_auth_pending_pair_t/spotify_auth_result_t) live
 * in spotify_auth_types.h, not here -- this header pulls in net_http.h for
 * its own function signatures below, which spotify_service_state.h (host-
 * tested) must never transitively include.
 */
#ifndef SPOTIFY_AUTH_H
#define SPOTIFY_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_http.h"
#include "spotify_auth_types.h"
#include "spotify_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generates a fresh PKCE verifier/challenge and state nonce (esp_random +
 * mbedtls sha256 + spotify_pkce_encode), builds the authorize URL. Returns
 * false if client_id/redirect_uri are empty or the URL doesn't fit. Thin
 * wrapper around spotify_auth_begin_pair_ex(..., NULL, NULL, ...). */
bool spotify_auth_begin_pair(const char *client_id, const char *redirect_uri,
                                char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX],
                                spotify_auth_pending_pair_t *out_pending,
                                char out_state[SPOTIFY_STATE_NONCE_MAX]);

/* Same as spotify_auth_begin_pair(), but fixed_verifier/fixed_state (either
 * or both, NULL falls back to random) are used verbatim instead of being
 * randomly generated -- the code_challenge is still correctly derived via
 * SHA-256 from whichever verifier is actually used. See
 * svc_spotify_begin_pair_with()'s own comment for why this exists
 * (CONFIG_DSC_SPOTIFY_AUTOPAIR's fixed test-only values). */
bool spotify_auth_begin_pair_ex(const char *client_id, const char *redirect_uri,
                                   const char *fixed_verifier, const char *fixed_state,
                                   char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX],
                                   spotify_auth_pending_pair_t *out_pending,
                                   char out_state[SPOTIFY_STATE_NONCE_MAX]);

/* POST accounts.spotify.com/api/token, grant_type=authorization_code. */
spotify_auth_result_t spotify_auth_exchange_code(net_http_t *http, const http_policy_t *policy,
                                                     const char *client_id, const char *redirect_uri,
                                                     const char *code, const char *code_verifier,
                                                     spotify_token_response_t *out);

/* POST accounts.spotify.com/api/token, grant_type=refresh_token. Spotify
 * may or may not rotate the refresh token on a given call -- out->refresh_token
 * is empty if it didn't; the caller keeps the old one in that case. */
spotify_auth_result_t spotify_auth_refresh(net_http_t *http, const http_policy_t *policy,
                                              const char *client_id, const char *refresh_token,
                                              spotify_token_response_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_AUTH_H */
