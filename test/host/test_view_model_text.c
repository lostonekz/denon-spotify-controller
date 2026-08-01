/*
 * Host test for view_model_text.c -- RISKS.md R17's "safety-critical part."
 * Deliberately adversarial: invalid leading bytes, stray continuation
 * bytes, overlong encodings, UTF-16 surrogates, and mid-codepoint
 * truncation boundaries.
 */
#include <string.h>

#include "unity.h"

#include "view_model_text.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ascii_copies_through_unchanged(void)
{
    char buf[16];
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "Fake Empire");
    TEST_ASSERT_EQUAL_STRING("Fake Empire", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("Fake Empire"), n);
}

static void test_null_src_produces_empty_string(void)
{
    char buf[8] = "leftovr";
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

static void test_zero_capacity_is_a_safe_no_op(void)
{
    char buf[4] = "xyz";
    size_t n = view_model_text_copy_safe(buf, 0, "hello");
    TEST_ASSERT_EQUAL_UINT(0, n);
    /* buf is untouched -- dst_cap==0 means there's no room for even a NUL. */
    TEST_ASSERT_EQUAL_STRING("xyz", buf);
}

static void test_valid_multibyte_codepoint_copies_whole(void)
{
    /* "Bjork" with an o-with-diaeresis: U+00F6, 2 bytes (0xC3 0xB6). */
    char buf[16];
    view_model_text_copy_safe(buf, sizeof(buf), "Bj\xC3\xB6rk");
    TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", buf);
}

static void test_truncation_never_splits_a_two_byte_codepoint(void)
{
    /* "a" + U+00E9 (0xC3 0xA9). Capacity for exactly "a" plus one more
     * byte is NOT enough room for the whole 2-byte codepoint -- it must
     * stop before it, not emit a dangling lead byte. */
    char buf[3];   /* budget = 2: room for 'a' + 1 more byte, not the full pair */
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "a\xC3\xA9");
    TEST_ASSERT_EQUAL_STRING("a", buf);
    TEST_ASSERT_EQUAL_UINT(1, n);
}

static void test_truncation_never_splits_a_three_byte_codepoint(void)
{
    /* U+2014 EM DASH, 3 bytes (0xE2 0x80 0x94). Exactly enough room for
     * an ASCII prefix but not the dash. */
    char buf[4];   /* budget = 3: fits "ab" (2) but not "ab" + the 3-byte dash */
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "ab\xE2\x80\x94");
    TEST_ASSERT_EQUAL_STRING("ab", buf);
    TEST_ASSERT_EQUAL_UINT(2, n);
}

static void test_truncation_never_splits_a_four_byte_codepoint(void)
{
    /* U+1F600 GRINNING FACE, 4 bytes (0xF0 0x9F 0x98 0x80), with exactly
     * enough room for the emoji alone -- but not for it plus more, and
     * not for a partial prefix of it either. */
    char buf[5];   /* budget = 4: fits the whole emoji, nothing else */
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "\xF0\x9F\x98\x80X");
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x98\x80", buf);
    TEST_ASSERT_EQUAL_UINT(4, n);
}

static void test_invalid_leading_byte_becomes_replacement_char(void)
{
    /* 0xFF is never valid anywhere in UTF-8. */
    char buf[16];
    view_model_text_copy_safe(buf, sizeof(buf), "a\xFFz");
    TEST_ASSERT_EQUAL_STRING("a\xEF\xBF\xBDz", buf);
}

static void test_stray_continuation_byte_becomes_replacement_char(void)
{
    /* 0x82 alone, with no leading byte before it, is a stray continuation
     * byte -- invalid on its own. */
    char buf[16];
    view_model_text_copy_safe(buf, sizeof(buf), "\x82z");
    TEST_ASSERT_EQUAL_STRING("\xEF\xBF\xBDz", buf);
}

static void test_truncated_continuation_sequence_replaces_each_bad_byte(void)
{
    /* 0xE2 0x82 alone (missing the 3rd byte a real 3-byte sequence needs)
     * -- the leading byte is invalid (not enough bytes follow), and the
     * orphaned continuation byte left behind is ALSO invalid on its own.
     * Two bad bytes in, two replacement characters out -- never a
     * corrupted frame, never silently dropped. */
    char buf[16];
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "\xE2\x82");
    TEST_ASSERT_EQUAL_STRING("\xEF\xBF\xBD\xEF\xBF\xBD", buf);
    TEST_ASSERT_EQUAL_UINT(6, n);
}

static void test_overlong_encoding_is_rejected(void)
{
    /* 0xC0 0x80 -- an overlong (invalid) encoding of NUL. A permissive
     * decoder that accepts this is a known real-world security footgun;
     * this one must not. */
    char buf[16];
    view_model_text_copy_safe(buf, sizeof(buf), "\xC0\x80");
    TEST_ASSERT_EQUAL_STRING("\xEF\xBF\xBD\xEF\xBF\xBD", buf);
}

static void test_utf16_surrogate_is_rejected(void)
{
    /* 0xED 0xA0 0x80 -- U+D800 encoded in UTF-8, a UTF-16 surrogate half
     * that must never appear as a real UTF-8 codepoint. */
    char buf[16];
    view_model_text_copy_safe(buf, sizeof(buf), "\xED\xA0\x80");
    TEST_ASSERT_EQUAL_STRING("\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD", buf);
}

static void test_replacement_char_itself_never_gets_split_on_truncation(void)
{
    /* Capacity too small for even one 3-byte replacement sequence -- must
     * stop rather than emit a truncated replacement character. */
    char buf[3];   /* budget = 2, replacement char needs 3 */
    size_t n = view_model_text_copy_safe(buf, sizeof(buf), "\xFF");
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

static void test_has_content_false_for_empty_string(void)
{
    TEST_ASSERT_FALSE(view_model_text_has_content(""));
}

static void test_has_content_false_for_null(void)
{
    TEST_ASSERT_FALSE(view_model_text_has_content(NULL));
}

static void test_has_content_false_for_entirely_replacement_chars(void)
{
    TEST_ASSERT_FALSE(view_model_text_has_content("\xEF\xBF\xBD\xEF\xBF\xBD"));
}

static void test_has_content_true_for_real_text(void)
{
    TEST_ASSERT_TRUE(view_model_text_has_content("Boxer"));
}

static void test_has_content_true_when_mixed_with_replacement_chars(void)
{
    TEST_ASSERT_TRUE(view_model_text_has_content("\xEF\xBF\xBDoxer"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ascii_copies_through_unchanged);
    RUN_TEST(test_null_src_produces_empty_string);
    RUN_TEST(test_zero_capacity_is_a_safe_no_op);
    RUN_TEST(test_valid_multibyte_codepoint_copies_whole);
    RUN_TEST(test_truncation_never_splits_a_two_byte_codepoint);
    RUN_TEST(test_truncation_never_splits_a_three_byte_codepoint);
    RUN_TEST(test_truncation_never_splits_a_four_byte_codepoint);
    RUN_TEST(test_invalid_leading_byte_becomes_replacement_char);
    RUN_TEST(test_stray_continuation_byte_becomes_replacement_char);
    RUN_TEST(test_truncated_continuation_sequence_replaces_each_bad_byte);
    RUN_TEST(test_overlong_encoding_is_rejected);
    RUN_TEST(test_utf16_surrogate_is_rejected);
    RUN_TEST(test_replacement_char_itself_never_gets_split_on_truncation);
    RUN_TEST(test_has_content_false_for_empty_string);
    RUN_TEST(test_has_content_false_for_null);
    RUN_TEST(test_has_content_false_for_entirely_replacement_chars);
    RUN_TEST(test_has_content_true_for_real_text);
    RUN_TEST(test_has_content_true_when_mixed_with_replacement_chars);
    return UNITY_END();
}
