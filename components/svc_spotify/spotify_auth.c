/*
 * See spotify_auth.h. Target-only: mbedtls sha256 + esp_random, no host
 * test path (same precedent as ota_verify.c).
 */
#include "spotify_auth.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"

#include "spotify_authorize_url.h"
#include "spotify_pkce_encode.h"

static const char *TAG = "spotify_auth";

#define SPOTIFY_TOKEN_URL "https://accounts.spotify.com/api/token"

static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

/* application/x-www-form-urlencoded value encoding -- same unreserved set
 * spotify_authorize_url.c uses for query-string values; duplicated here
 * (a different translation unit, and small enough not to warrant a shared
 * module) rather than exported from that file. */
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

static void random_hex(char *out, size_t out_cap, size_t n_bytes)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[32];
    if (n_bytes > sizeof(raw)) {
        n_bytes = sizeof(raw);
    }
    esp_fill_random(raw, n_bytes);

    size_t o = 0;
    for (size_t i = 0; i < n_bytes && o + 2 < out_cap; i++) {
        out[o++] = hex[raw[i] >> 4];
        out[o++] = hex[raw[i] & 0x0Fu];
    }
    out[o] = '\0';
}

bool spotify_auth_begin_pair(const char *client_id, const char *redirect_uri,
                                char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX],
                                spotify_auth_pending_pair_t *out_pending,
                                char out_state[SPOTIFY_STATE_NONCE_MAX])
{
    return spotify_auth_begin_pair_ex(client_id, redirect_uri, NULL, NULL,
                                          out_authorize_url, out_pending, out_state);
}

bool spotify_auth_begin_pair_ex(const char *client_id, const char *redirect_uri,
                                   const char *fixed_verifier, const char *fixed_state,
                                   char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX],
                                   spotify_auth_pending_pair_t *out_pending,
                                   char out_state[SPOTIFY_STATE_NONCE_MAX])
{
    if (client_id == NULL || client_id[0] == '\0' ||
        redirect_uri == NULL || redirect_uri[0] == '\0' ||
        out_authorize_url == NULL || out_pending == NULL || out_state == NULL) {
        return false;
    }

    if (fixed_verifier != NULL) {
        strncpy(out_pending->code_verifier, fixed_verifier, sizeof(out_pending->code_verifier) - 1);
        out_pending->code_verifier[sizeof(out_pending->code_verifier) - 1] = '\0';
    } else {
        /* code_verifier: 32 random bytes, base64url-encoded (43 chars) --
         * RFC 7636 §4.1 recommends exactly this. */
        uint8_t verifier_bytes[32];
        esp_fill_random(verifier_bytes, sizeof(verifier_bytes));
        if (spotify_base64url_encode(verifier_bytes, sizeof(verifier_bytes),
                                       out_pending->code_verifier, sizeof(out_pending->code_verifier)) == 0) {
            return false;
        }
    }

    /* code_challenge = base64url(SHA-256(code_verifier)) -- the ASCII
     * verifier's bytes, per RFC 7636 §4.2's S256 method. Derived the same
     * way regardless of where the verifier came from. */
    uint8_t digest[32];
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    mbedtls_sha256_update(&sha_ctx, (const unsigned char *)out_pending->code_verifier,
                            strlen(out_pending->code_verifier));
    mbedtls_sha256_finish(&sha_ctx, digest);
    mbedtls_sha256_free(&sha_ctx);

    char challenge[64];
    if (spotify_base64url_encode(digest, sizeof(digest), challenge, sizeof(challenge)) == 0) {
        return false;
    }

    if (fixed_state != NULL) {
        strncpy(out_state, fixed_state, SPOTIFY_STATE_NONCE_MAX - 1);
        out_state[SPOTIFY_STATE_NONCE_MAX - 1] = '\0';
    } else {
        random_hex(out_state, SPOTIFY_STATE_NONCE_MAX, 16);
    }

    spotify_authorize_url_params_t params = {
        .client_id      = client_id,
        .redirect_uri   = redirect_uri,
        .code_challenge = challenge,
        .state          = out_state,
    };
    return spotify_authorize_url_build(&params, out_authorize_url, SPOTIFY_AUTHORIZE_URL_MAX) > 0;
}

static spotify_auth_result_t post_token_request(net_http_t *http, const http_policy_t *policy,
                                                    const char *body, size_t body_len,
                                                    spotify_token_response_t *out)
{
    memset(out, 0, sizeof(*out));

    char resp_buf[2048];
    net_http_response_t resp = { 0 };
    net_http_request_opts_t opts = {
        .method               = NET_HTTP_METHOD_POST,
        .bearer_token         = NULL,   /* PKCE public client -- no client_secret, no auth header */
        .body                 = { .content_type = "application/x-www-form-urlencoded",
                                    .body = body, .body_len = body_len },
        /* The token endpoint is only ever called once per pairing attempt
         * or once per scheduled refresh -- retrying an ambiguous timeout
         * risks presenting an already-consumed code, or (for refresh) a
         * refresh token Spotify may have already rotated server-side. See
         * this project's Phase 4 plan §6/§2. */
        .retryable_after_send = false,
    };

    if (!net_http_request(http, SPOTIFY_TOKEN_URL, &opts, policy, resp_buf, sizeof(resp_buf), &resp)) {
        ESP_LOGW(TAG, "token request: transport failure");
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    spotify_token_parse_result_t parsed = spotify_parse_token_response(resp_buf, resp.len, out);
    switch (parsed) {
    case SPOTIFY_TOKEN_RESULT_OK:
        return SPOTIFY_AUTH_RESULT_OK;
    case SPOTIFY_TOKEN_RESULT_INVALID_GRANT:
        ESP_LOGW(TAG, "token request: invalid_grant");
        return SPOTIFY_AUTH_RESULT_FAILED_AUTH;
    case SPOTIFY_TOKEN_RESULT_OTHER_ERROR:
    case SPOTIFY_TOKEN_RESULT_MALFORMED:
    default:
        ESP_LOGW(TAG, "token request: status=%d parse_result=%d", resp.status_code, (int)parsed);
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }
}

spotify_auth_result_t spotify_auth_exchange_code(net_http_t *http, const http_policy_t *policy,
                                                     const char *client_id, const char *redirect_uri,
                                                     const char *code, const char *code_verifier,
                                                     spotify_token_response_t *out)
{
    if (http == NULL || policy == NULL || client_id == NULL || redirect_uri == NULL ||
        code == NULL || code_verifier == NULL || out == NULL) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    /* Real Spotify authorization codes run ~330-380 chars (confirmed against
     * real hardware pairing attempts) -- found on real hardware: a 256-byte
     * code_encoded buffer silently failed percent_encode() (returns 0 on
     * overflow) for every real code, so this returned FAILED_TRANSIENT
     * before ever making the HTTP request, misleadingly fast and
     * misleadingly classified as "just retry" when it was a hard local
     * buffer-size bug. */
    char redirect_encoded[256];
    char code_encoded[512];
    if (percent_encode(redirect_uri, redirect_encoded, sizeof(redirect_encoded)) == 0 ||
        percent_encode(code, code_encoded, sizeof(code_encoded)) == 0) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    char body[1200];
    int n = snprintf(body, sizeof(body),
        "grant_type=authorization_code&code=%s&redirect_uri=%s&client_id=%s&code_verifier=%s",
        code_encoded, redirect_encoded, client_id, code_verifier);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    return post_token_request(http, policy, body, (size_t)n, out);
}

spotify_auth_result_t spotify_auth_refresh(net_http_t *http, const http_policy_t *policy,
                                              const char *client_id, const char *refresh_token,
                                              spotify_token_response_t *out)
{
    if (http == NULL || policy == NULL || client_id == NULL || refresh_token == NULL || out == NULL) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    char token_encoded[SPOTIFY_REFRESH_TOKEN_MAX * 3];
    if (percent_encode(refresh_token, token_encoded, sizeof(token_encoded)) == 0) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    char body[900];
    int n = snprintf(body, sizeof(body),
        "grant_type=refresh_token&refresh_token=%s&client_id=%s", token_encoded, client_id);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
    }

    return post_token_request(http, policy, body, (size_t)n, out);
}
