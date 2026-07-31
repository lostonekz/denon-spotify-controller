/*
 * Host test for spotify_reauth_watch: wall-clock threshold math around the
 * ~5.5mo warning boundary, using fixed epoch-second values (no clock
 * dependency of any kind -- see the header's own comment on why this
 * deliberately isn't wall-clock or monotonic time from a fake clock).
 */

#include "unity.h"

#include "spotify_reauth_watch.h"

void setUp(void) {}
void tearDown(void) {}

#define DAY_S ((int64_t)24 * 3600)

static void test_unset_paired_at_is_unknown(void)
{
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_UNKNOWN, spotify_reauth_check(0, 1780000000));
}

static void test_clock_went_backward_is_unknown(void)
{
    int64_t paired_at = 1780000000;
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_UNKNOWN, spotify_reauth_check(paired_at, paired_at - 1));
}

static void test_just_paired_is_not_due(void)
{
    int64_t paired_at = 1780000000;
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_NOT_DUE, spotify_reauth_check(paired_at, paired_at));
}

static void test_one_day_short_of_threshold_is_not_due(void)
{
    int64_t paired_at = 1780000000;
    int64_t now = paired_at + (165 * DAY_S) - DAY_S;
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_NOT_DUE, spotify_reauth_check(paired_at, now));
}

static void test_exactly_at_threshold_is_due_soon(void)
{
    int64_t paired_at = 1780000000;
    int64_t now = paired_at + (165 * DAY_S);
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_DUE_SOON, spotify_reauth_check(paired_at, now));
}

static void test_well_past_threshold_is_due_soon(void)
{
    int64_t paired_at = 1780000000;
    int64_t now = paired_at + (200 * DAY_S);
    TEST_ASSERT_EQUAL(SPOTIFY_REAUTH_DUE_SOON, spotify_reauth_check(paired_at, now));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unset_paired_at_is_unknown);
    RUN_TEST(test_clock_went_backward_is_unknown);
    RUN_TEST(test_just_paired_is_not_due);
    RUN_TEST(test_one_day_short_of_threshold_is_not_due);
    RUN_TEST(test_exactly_at_threshold_is_due_soon);
    RUN_TEST(test_well_past_threshold_is_due_soon);
    return UNITY_END();
}
