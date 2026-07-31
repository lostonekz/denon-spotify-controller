/*
 * Host test for spotify_pkce_encode: base64url-no-pad encoding against the
 * standard RFC 4648 test vectors (none of these bytes produce '+'/'/', so
 * they double as base64url vectors unmodified -- padding is simply absent).
 */

#include "unity.h"

#include "spotify_pkce_encode.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void check(const char *input, const char *expected)
{
    char out[64];
    size_t len = spotify_base64url_encode((const uint8_t *)input, strlen(input), out, sizeof(out));
    TEST_ASSERT_EQUAL_MESSAGE(strlen(expected), len, input);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out, input);
}

static void test_rfc4648_vectors(void)
{
    check("", "");
    check("f", "Zg");
    check("fo", "Zm8");
    check("foo", "Zm9v");
    check("foob", "Zm9vYg");
    check("fooba", "Zm9vYmE");
    check("foobar", "Zm9vYmFy");
}

static void test_no_padding_characters_ever_appear(void)
{
    char out[64];
    for (size_t n = 0; n <= 6; n++) {
        uint8_t data[6] = { 0, 1, 2, 3, 4, 5 };
        spotify_base64url_encode(data, n, out, sizeof(out));
        TEST_ASSERT_NULL(strchr(out, '='));
    }
}

static void test_url_safe_alphabet_used_for_bytes_that_would_produce_plus_or_slash(void)
{
    /* 0xFB 0xFF 0xBF encodes to "+/+/" in standard base64 -- must come out
     * as "-_-_" here instead. */
    uint8_t data[3] = { 0xFB, 0xFF, 0xBF };
    char out[8];
    spotify_base64url_encode(data, sizeof(data), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-_-_", out);
}

static void test_output_buffer_too_small_returns_zero(void)
{
    char out[4];
    size_t len = spotify_base64url_encode((const uint8_t *)"foobar", 6, out, sizeof(out));
    TEST_ASSERT_EQUAL(0, len);
}

static void test_encoded_len_matches_actual_output(void)
{
    for (size_t n = 0; n <= 32; n++) {
        uint8_t data[32] = { 0 };
        char out[64];
        size_t actual = spotify_base64url_encode(data, n, out, sizeof(out));
        TEST_ASSERT_EQUAL(spotify_base64url_encoded_len(n), actual);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc4648_vectors);
    RUN_TEST(test_no_padding_characters_ever_appear);
    RUN_TEST(test_url_safe_alphabet_used_for_bytes_that_would_produce_plus_or_slash);
    RUN_TEST(test_output_buffer_too_small_returns_zero);
    RUN_TEST(test_encoded_len_matches_actual_output);
    return UNITY_END();
}
