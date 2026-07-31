/*
 * Host test for net_http_retry_policy: every retry/backoff/Retry-After
 * branch from docs/NETWORKING.md §1.6's http_policy_t.
 */

#include "unity.h"

#include "net_http_retry_policy.h"

static const http_policy_t DEFAULT_POLICY = {
    .connect_timeout_ms = 5000,
    .request_timeout_ms = 8000,
    .max_retries        = 2,
    .backoff_base_ms    = 500,
    .retry_on_5xx       = true,
    .honour_retry_after = true,
};

void setUp(void) {}
void tearDown(void) {}

static void test_success_never_retries(void)
{
    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&DEFAULT_POLICY, 0, 200, false, 0, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_NO, d);
}

static void test_non_retryable_4xx_does_not_retry(void)
{
    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&DEFAULT_POLICY, 0, 404, false, 0, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_NO, d);
}

static void test_timeout_retries_with_exponential_backoff(void)
{
    uint32_t delay;
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, net_http_retry_decide(&DEFAULT_POLICY, 0, 0, true, 0, &delay));
    TEST_ASSERT_EQUAL_UINT32(500, delay);

    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, net_http_retry_decide(&DEFAULT_POLICY, 1, 0, true, 0, &delay));
    TEST_ASSERT_EQUAL_UINT32(1000, delay);
}

static void test_5xx_retries_when_policy_allows(void)
{
    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&DEFAULT_POLICY, 0, 503, false, 0, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, d);
    TEST_ASSERT_EQUAL_UINT32(500, delay);
}

static void test_5xx_does_not_retry_when_policy_disallows(void)
{
    http_policy_t policy = DEFAULT_POLICY;
    policy.retry_on_5xx = false;

    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&policy, 0, 503, false, 0, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_NO, d);
}

static void test_429_honours_retry_after_exactly(void)
{
    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&DEFAULT_POLICY, 0, 429, false, 30, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, d);
    TEST_ASSERT_EQUAL_UINT32(30000, delay);
}

static void test_429_without_retry_after_honouring_falls_back_to_backoff(void)
{
    http_policy_t policy = DEFAULT_POLICY;
    policy.honour_retry_after = false;

    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&policy, 0, 429, false, 30, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, d);
    TEST_ASSERT_EQUAL_UINT32(500, delay);   /* exponential backoff, not the server's 30s */
}

static void test_429_with_zero_retry_after_falls_back_to_backoff(void)
{
    uint32_t delay;
    http_retry_decision_t d = net_http_retry_decide(&DEFAULT_POLICY, 0, 429, false, 0, &delay);
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, d);
    TEST_ASSERT_EQUAL_UINT32(500, delay);
}

static void test_retries_exhausted_stops_regardless_of_retryability(void)
{
    uint32_t delay;
    /* max_retries = 2: attempts 0 and 1 may retry, attempt 2 must not. */
    TEST_ASSERT_EQUAL(HTTP_RETRY_YES, net_http_retry_decide(&DEFAULT_POLICY, 1, 0, true, 0, &delay));
    TEST_ASSERT_EQUAL(HTTP_RETRY_NO, net_http_retry_decide(&DEFAULT_POLICY, 2, 0, true, 0, &delay));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_never_retries);
    RUN_TEST(test_non_retryable_4xx_does_not_retry);
    RUN_TEST(test_timeout_retries_with_exponential_backoff);
    RUN_TEST(test_5xx_retries_when_policy_allows);
    RUN_TEST(test_5xx_does_not_retry_when_policy_disallows);
    RUN_TEST(test_429_honours_retry_after_exactly);
    RUN_TEST(test_429_without_retry_after_honouring_falls_back_to_backoff);
    RUN_TEST(test_429_with_zero_retry_after_falls_back_to_backoff);
    RUN_TEST(test_retries_exhausted_stops_regardless_of_retryability);
    return UNITY_END();
}
