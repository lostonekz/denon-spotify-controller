/*
 * Host test for net_wifi_backoff: exponential growth and the 60s cap
 * (checked via the pure max_delay_ms(), independent of jitter), and the
 * jitter/attempt-counter/reset behavior of the stateful API.
 */

#include "unity.h"

#include "net_wifi_backoff.h"

static uint32_t g_fixed_rand_value;
static int      g_rand_calls;

static uint32_t fake_rand_fixed(void *ctx)
{
    (void)ctx;
    g_rand_calls++;
    return g_fixed_rand_value;
}

void setUp(void)
{
    g_fixed_rand_value = 0;
    g_rand_calls = 0;
}

void tearDown(void) {}

static void test_max_delay_grows_exponentially_then_caps(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000,  net_wifi_backoff_max_delay_ms(0));
    TEST_ASSERT_EQUAL_UINT32(2000,  net_wifi_backoff_max_delay_ms(1));
    TEST_ASSERT_EQUAL_UINT32(4000,  net_wifi_backoff_max_delay_ms(2));
    TEST_ASSERT_EQUAL_UINT32(8000,  net_wifi_backoff_max_delay_ms(3));
    TEST_ASSERT_EQUAL_UINT32(16000, net_wifi_backoff_max_delay_ms(4));
    TEST_ASSERT_EQUAL_UINT32(32000, net_wifi_backoff_max_delay_ms(5));
    /* base<<6 = 64000, exceeds the cap -- pinned at 60000 from here on. */
    TEST_ASSERT_EQUAL_UINT32(60000, net_wifi_backoff_max_delay_ms(6));
    TEST_ASSERT_EQUAL_UINT32(60000, net_wifi_backoff_max_delay_ms(7));
    TEST_ASSERT_EQUAL_UINT32(60000, net_wifi_backoff_max_delay_ms(100));
}

static void test_next_delay_with_zero_rand_is_zero(void)
{
    net_wifi_backoff_t b;
    net_wifi_backoff_init(&b, fake_rand_fixed, NULL);
    g_fixed_rand_value = 0;

    TEST_ASSERT_EQUAL_UINT32(0, net_wifi_backoff_next_delay_ms(&b));
    TEST_ASSERT_EQUAL_UINT32(0, net_wifi_backoff_next_delay_ms(&b));
}

static void test_next_delay_never_exceeds_max_for_that_attempt(void)
{
    net_wifi_backoff_t b;
    net_wifi_backoff_init(&b, fake_rand_fixed, NULL);
    g_fixed_rand_value = 0xFFFFFFFFu;

    for (uint32_t attempt = 0; attempt < 10; attempt++) {
        uint32_t expected_max = net_wifi_backoff_max_delay_ms(attempt);
        uint32_t delay = net_wifi_backoff_next_delay_ms(&b);
        TEST_ASSERT_TRUE(delay <= expected_max);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu % (expected_max + 1), delay);
    }
}

static void test_attempt_counter_increments_on_each_call(void)
{
    net_wifi_backoff_t b;
    net_wifi_backoff_init(&b, fake_rand_fixed, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, b.attempt);
    net_wifi_backoff_next_delay_ms(&b);
    TEST_ASSERT_EQUAL_UINT32(1, b.attempt);
    net_wifi_backoff_next_delay_ms(&b);
    net_wifi_backoff_next_delay_ms(&b);
    TEST_ASSERT_EQUAL_UINT32(3, b.attempt);
}

static void test_reset_returns_attempt_to_zero(void)
{
    net_wifi_backoff_t b;
    net_wifi_backoff_init(&b, fake_rand_fixed, NULL);

    net_wifi_backoff_next_delay_ms(&b);
    net_wifi_backoff_next_delay_ms(&b);
    TEST_ASSERT_EQUAL_UINT32(2, b.attempt);

    net_wifi_backoff_reset(&b);
    TEST_ASSERT_EQUAL_UINT32(0, b.attempt);
}

static void test_rand_is_called_exactly_once_per_delay(void)
{
    net_wifi_backoff_t b;
    net_wifi_backoff_init(&b, fake_rand_fixed, NULL);

    net_wifi_backoff_next_delay_ms(&b);
    TEST_ASSERT_EQUAL(1, g_rand_calls);
    net_wifi_backoff_next_delay_ms(&b);
    TEST_ASSERT_EQUAL(2, g_rand_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_max_delay_grows_exponentially_then_caps);
    RUN_TEST(test_next_delay_with_zero_rand_is_zero);
    RUN_TEST(test_next_delay_never_exceeds_max_for_that_attempt);
    RUN_TEST(test_attempt_counter_increments_on_each_call);
    RUN_TEST(test_reset_returns_attempt_to_zero);
    RUN_TEST(test_rand_is_called_exactly_once_per_delay);
    return UNITY_END();
}
