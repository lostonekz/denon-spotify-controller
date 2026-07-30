/*
 * Host test for svc_logging_redact: prefix-anchored scrubbing against a
 * synthetic secret string, proving the mechanism ahead of any real Phase 1
 * consumer (empty ruleset in production use so far).
 */

#include "unity.h"

#include "svc_logging_redact.h"

#include <string.h>

static char buf[256];

static size_t load(const char *text)
{
    size_t len = strlen(text);
    memcpy(buf, text, len);
    return len;
}

void setUp(void) { memset(buf, 0, sizeof(buf)); }
void tearDown(void) {}

static void test_empty_ruleset_redacts_nothing(void)
{
    size_t len = load("Authorization: Bearer sk-super-secret-token-123");
    size_t n = svc_logging_redact_apply(buf, len, NULL, 0);

    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer sk-super-secret-token-123", buf);
}

static void test_matching_prefix_redacts_value_up_to_whitespace(void)
{
    redact_rule_t rules[] = { { .prefix = "Authorization: Bearer " } };
    size_t len = load("Authorization: Bearer sk-super-secret-token-123 done");

    size_t n = svc_logging_redact_apply(buf, len, rules, 1);

    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer ************************* done", buf);
}

static void test_no_matching_prefix_leaves_text_untouched(void)
{
    redact_rule_t rules[] = { { .prefix = "password=" } };
    size_t len = load("just a normal log line, nothing sensitive here");

    size_t n = svc_logging_redact_apply(buf, len, rules, 1);

    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL_STRING("just a normal log line, nothing sensitive here", buf);
}

static void test_redaction_stops_at_end_of_buffer_when_no_whitespace_follows(void)
{
    redact_rule_t rules[] = { { .prefix = "token=" } };
    size_t len = load("token=abcdef");

    size_t n = svc_logging_redact_apply(buf, len, rules, 1);

    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("token=******", buf);
}

static void test_multiple_occurrences_in_one_line_all_redacted(void)
{
    redact_rule_t rules[] = { { .prefix = "token=" } };
    size_t len = load("token=first and token=second end");

    size_t n = svc_logging_redact_apply(buf, len, rules, 1);

    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("token=***** and token=****** end", buf);
}

static void test_multiple_rules_each_independently_matched(void)
{
    redact_rule_t rules[] = {
        { .prefix = "token=" },
        { .prefix = "password=" },
    };
    size_t len = load("token=abc password=xyz");

    size_t n = svc_logging_redact_apply(buf, len, rules, 2);

    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("token=*** password=***", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_ruleset_redacts_nothing);
    RUN_TEST(test_matching_prefix_redacts_value_up_to_whitespace);
    RUN_TEST(test_no_matching_prefix_leaves_text_untouched);
    RUN_TEST(test_redaction_stops_at_end_of_buffer_when_no_whitespace_follows);
    RUN_TEST(test_multiple_occurrences_in_one_line_all_redacted);
    RUN_TEST(test_multiple_rules_each_independently_matched);
    return UNITY_END();
}
