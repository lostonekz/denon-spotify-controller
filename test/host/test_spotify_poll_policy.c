/*
 * Host test for spotify_poll_policy: all 4 conditions, force-poll deadline
 * math, and consecutive-error backoff to the 120s cap. The "asleep"
 * conditions are exercised here despite no real producer yet (svc_power
 * doesn't exist until Phase 6) -- same treatment as denon_link_fsm's dead
 * SLEEP_ENTER/WAKE pair in Phase 3.
 */

#include "unity.h"

#include "spotify_poll_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_playing_awake_is_5s(void)
{
    spotify_poll_condition_t c = { .is_playing = true, .screen_awake = true };
    TEST_ASSERT_EQUAL_INT64(5000000, spotify_poll_interval_us(&c));
}

static void test_playing_asleep_is_30s(void)
{
    spotify_poll_condition_t c = { .is_playing = true, .screen_awake = false };
    TEST_ASSERT_EQUAL_INT64(30000000, spotify_poll_interval_us(&c));
}

static void test_paused_awake_is_10s(void)
{
    spotify_poll_condition_t c = { .is_playing = false, .screen_awake = true };
    TEST_ASSERT_EQUAL_INT64(10000000, spotify_poll_interval_us(&c));
}

static void test_paused_asleep_is_60s(void)
{
    spotify_poll_condition_t c = { .is_playing = false, .screen_awake = false };
    TEST_ASSERT_EQUAL_INT64(60000000, spotify_poll_interval_us(&c));
}

static void test_force_poll_deadlines(void)
{
    int64_t submitted = 1000000000;
    TEST_ASSERT_EQUAL_INT64(submitted + 250000, spotify_poll_force_deadline_us(submitted, 0));
    TEST_ASSERT_EQUAL_INT64(submitted + 1000000, spotify_poll_force_deadline_us(submitted, 1));
    TEST_ASSERT_EQUAL_INT64(0, spotify_poll_force_deadline_us(submitted, 2));
}

static void test_backoff_zero_errors_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT64(0, spotify_poll_backoff_us(0));
}

static void test_backoff_doubles_each_error(void)
{
    TEST_ASSERT_EQUAL_INT64(5000000,  spotify_poll_backoff_us(1));
    TEST_ASSERT_EQUAL_INT64(10000000, spotify_poll_backoff_us(2));
    TEST_ASSERT_EQUAL_INT64(20000000, spotify_poll_backoff_us(3));
    TEST_ASSERT_EQUAL_INT64(40000000, spotify_poll_backoff_us(4));
    TEST_ASSERT_EQUAL_INT64(80000000, spotify_poll_backoff_us(5));
}

static void test_backoff_caps_at_120s(void)
{
    TEST_ASSERT_EQUAL_INT64(120000000, spotify_poll_backoff_us(6));
    TEST_ASSERT_EQUAL_INT64(120000000, spotify_poll_backoff_us(7));
    TEST_ASSERT_EQUAL_INT64(120000000, spotify_poll_backoff_us(1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_playing_awake_is_5s);
    RUN_TEST(test_playing_asleep_is_30s);
    RUN_TEST(test_paused_awake_is_10s);
    RUN_TEST(test_paused_asleep_is_60s);
    RUN_TEST(test_force_poll_deadlines);
    RUN_TEST(test_backoff_zero_errors_is_zero);
    RUN_TEST(test_backoff_doubles_each_error);
    RUN_TEST(test_backoff_caps_at_120s);
    return UNITY_END();
}
