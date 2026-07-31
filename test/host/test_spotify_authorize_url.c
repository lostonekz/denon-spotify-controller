/*
 * Host test for spotify_authorize_url: asserts the built URL carries every
 * required param, correctly percent-encoded where needed, and critically
 * the exact scope string -- an earlier draft of this plan omitted `scope`
 * entirely, which would have made pairing succeed while every real player
 * call afterward returned 403.
 */

#include "unity.h"

#include "spotify_authorize_url.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_build_contains_all_required_params(void)
{
    spotify_authorize_url_params_t p = {
        .client_id      = "abc123def456",
        .redirect_uri   = "http://127.0.0.1:8888/callback",
        .code_challenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
        .state          = "deadbeefcafef00d",
    };
    char out[512];
    size_t len = spotify_authorize_url_build(&p, out, sizeof(out));

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL(strlen(out), len);
    TEST_ASSERT_NOT_NULL(strstr(out, "https://accounts.spotify.com/authorize?"));
    TEST_ASSERT_NOT_NULL(strstr(out, "client_id=abc123def456"));
    TEST_ASSERT_NOT_NULL(strstr(out, "response_type=code"));
    TEST_ASSERT_NOT_NULL(strstr(out, "code_challenge_method=S256"));
    TEST_ASSERT_NOT_NULL(strstr(out, "code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    TEST_ASSERT_NOT_NULL(strstr(out, "state=deadbeefcafef00d"));
}

/* The one bug this test exists to catch: an earlier draft built a URL with
 * no scope parameter at all. */
static void test_build_contains_exact_scope_string(void)
{
    spotify_authorize_url_params_t p = {
        .client_id      = "abc123def456",
        .redirect_uri   = "http://127.0.0.1:8888/callback",
        .code_challenge = "challenge",
        .state          = "state123",
    };
    char out[512];
    spotify_authorize_url_build(&p, out, sizeof(out));

    TEST_ASSERT_NOT_NULL(strstr(out,
        "scope=user-read-playback-state%20user-modify-playback-state"));
}

static void test_redirect_uri_is_percent_encoded(void)
{
    spotify_authorize_url_params_t p = {
        .client_id      = "abc123def456",
        .redirect_uri   = "http://127.0.0.1:8888/callback",
        .code_challenge = "challenge",
        .state          = "state123",
    };
    char out[512];
    spotify_authorize_url_build(&p, out, sizeof(out));

    /* The raw redirect_uri must NOT appear unencoded (its ':' and '/'
     * characters are not valid unescaped inside a query-string value). */
    TEST_ASSERT_NULL(strstr(out, "redirect_uri=http://"));
    TEST_ASSERT_NOT_NULL(strstr(out, "redirect_uri=http%3A%2F%2F127.0.0.1%3A8888%2Fcallback"));
}

static void test_missing_required_param_returns_zero(void)
{
    spotify_authorize_url_params_t p = {
        .client_id      = "abc123def456",
        .redirect_uri   = "http://127.0.0.1:8888/callback",
        .code_challenge = "challenge",
        .state          = NULL,
    };
    char out[512];
    TEST_ASSERT_EQUAL(0, spotify_authorize_url_build(&p, out, sizeof(out)));
}

static void test_output_buffer_too_small_returns_zero(void)
{
    spotify_authorize_url_params_t p = {
        .client_id      = "abc123def456",
        .redirect_uri   = "http://127.0.0.1:8888/callback",
        .code_challenge = "challenge",
        .state          = "state123",
    };
    char tiny[8];
    TEST_ASSERT_EQUAL(0, spotify_authorize_url_build(&p, tiny, sizeof(tiny)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_contains_all_required_params);
    RUN_TEST(test_build_contains_exact_scope_string);
    RUN_TEST(test_redirect_uri_is_percent_encoded);
    RUN_TEST(test_missing_required_param_returns_zero);
    RUN_TEST(test_output_buffer_too_small_returns_zero);
    return UNITY_END();
}
