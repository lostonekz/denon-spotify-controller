/*
 * Pure PKCE/auth-result types, split out of spotify_auth.h so
 * spotify_service_state.h (host-tested, no ESP-IDF) can use them without
 * pulling in net_http.h (FreeRTOS-tainted) -- spotify_auth.h's own
 * function declarations are the only impure part; this file is that
 * split, same reasoning as denon_service_state.h's own top comment.
 */
#ifndef SPOTIFY_AUTH_TYPES_H
#define SPOTIFY_AUTH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_PKCE_VERIFIER_MAX 64    /* 32 random bytes -> 43 base64url chars + NUL, headroom */
#define SPOTIFY_STATE_NONCE_MAX   40    /* 16 random bytes -> 32 hex chars + NUL, headroom */
#define SPOTIFY_AUTHORIZE_URL_MAX 512

typedef struct {
    char code_verifier[SPOTIFY_PKCE_VERIFIER_MAX];
} spotify_auth_pending_pair_t;

typedef enum {
    SPOTIFY_AUTH_RESULT_OK = 0,
    /* "error":"invalid_grant" -- a genuinely dead code/refresh token, per
     * this project's Phase 4 plan §2 the ONLY case that goes straight to
     * REAUTH_REQUIRED with no retry. */
    SPOTIFY_AUTH_RESULT_FAILED_AUTH,
    /* Timeout, 5xx, or a malformed/unexpected response -- retried with
     * backoff by the caller's own FSM, never auto-escalated to
     * FAILED_AUTH from repetition alone. */
    SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT,
} spotify_auth_result_t;

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_AUTH_TYPES_H */
