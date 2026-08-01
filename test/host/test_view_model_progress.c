/*
 * Host test for view_model_progress.c -- Phase 5 Tier 1.
 */
#include "unity.h"

#include "view_model_progress.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- view_model_progress_displayed_ms() --------------------------------
 * Same test shape as spotify_progress.c's own test_spotify_progress.c --
 * this is a deliberate duplicate of that exact formula (see this module's
 * header comment for why), so it earns the same coverage independently. */

static void test_paused_returns_progress_unchanged(void)
{
    int64_t sampled_at_us = 1000000000;
    int64_t now_us         = sampled_at_us + 10 * 1000000;   /* 10s later -- must not matter while paused */
    TEST_ASSERT_EQUAL_UINT32(42000, view_model_progress_displayed_ms(42000, sampled_at_us, false, now_us));
}

static void test_playing_interpolates_forward(void)
{
    int64_t sampled_at_us = 1000000000;
    int64_t now_us         = sampled_at_us + 3 * 1000000;   /* 3s later */
    TEST_ASSERT_EQUAL_UINT32(45000, view_model_progress_displayed_ms(42000, sampled_at_us, true, now_us));
}

static void test_playing_at_exact_sample_time_is_unchanged(void)
{
    int64_t sampled_at_us = 1000000000;
    TEST_ASSERT_EQUAL_UINT32(42000, view_model_progress_displayed_ms(42000, sampled_at_us, true, sampled_at_us));
}

static void test_fix_from_the_future_does_not_go_negative(void)
{
    int64_t now_us = 1000000000;
    int64_t sampled_at_us = now_us + 5000000;   /* implausible: sampled after "now" */
    TEST_ASSERT_EQUAL_UINT32(42000, view_model_progress_displayed_ms(42000, sampled_at_us, true, now_us));
}

/* ---- view_model_progress_permille() ------------------------------------ */

static void test_zero_duration_is_zero_not_a_divide_by_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, view_model_progress_permille(5000, 0));
}

static void test_zero_elapsed_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, view_model_progress_permille(0, 127000));
}

static void test_halfway(void)
{
    TEST_ASSERT_EQUAL_UINT16(500, view_model_progress_permille(63500, 127000));
}

static void test_exactly_at_duration_is_1000(void)
{
    TEST_ASSERT_EQUAL_UINT16(1000, view_model_progress_permille(127000, 127000));
}

static void test_overshoot_past_duration_clamps_to_1000(void)
{
    /* A brief interpolation overshoot near track end, before the next
     * poll corrects it -- must clamp, never wrap past 1000. */
    TEST_ASSERT_EQUAL_UINT16(1000, view_model_progress_permille(130000, 127000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_paused_returns_progress_unchanged);
    RUN_TEST(test_playing_interpolates_forward);
    RUN_TEST(test_playing_at_exact_sample_time_is_unchanged);
    RUN_TEST(test_fix_from_the_future_does_not_go_negative);
    RUN_TEST(test_zero_duration_is_zero_not_a_divide_by_zero);
    RUN_TEST(test_zero_elapsed_is_zero);
    RUN_TEST(test_halfway);
    RUN_TEST(test_exactly_at_duration_is_1000);
    RUN_TEST(test_overshoot_past_duration_clamps_to_1000);
    return UNITY_END();
}
