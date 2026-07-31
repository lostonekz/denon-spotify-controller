/*
 * Host test for spotify_progress: adr/0004's fix-plus-offset math,
 * pause-freezes-interpolation, and no negative-elapsed weirdness if a fix
 * is (implausibly) timestamped after "now".
 */

#include "unity.h"

#include "spotify_progress.h"
#include "fake_time_source.h"

static fake_time_source_t g_fake;
static time_source_t g_clock;

void setUp(void)
{
    g_fake.now_us = 1000000000;
    g_clock = fake_time_source_get(&g_fake);
}

void tearDown(void) {}

static void test_paused_returns_progress_unchanged(void)
{
    playback_fix_t fix = { .progress_ms = 42000, .sampled_at_us = g_fake.now_us, .is_playing = false };
    g_fake.now_us += 10 * 1000000;   /* 10s later -- must not matter while paused */
    TEST_ASSERT_EQUAL_UINT32(42000, spotify_progress_displayed_ms(&fix, g_clock));
}

static void test_playing_interpolates_forward(void)
{
    playback_fix_t fix = { .progress_ms = 42000, .sampled_at_us = g_fake.now_us, .is_playing = true };
    g_fake.now_us += 3 * 1000000;   /* 3s later */
    TEST_ASSERT_EQUAL_UINT32(45000, spotify_progress_displayed_ms(&fix, g_clock));
}

static void test_playing_at_exact_sample_time_is_unchanged(void)
{
    playback_fix_t fix = { .progress_ms = 42000, .sampled_at_us = g_fake.now_us, .is_playing = true };
    TEST_ASSERT_EQUAL_UINT32(42000, spotify_progress_displayed_ms(&fix, g_clock));
}

static void test_fix_from_the_future_does_not_go_negative(void)
{
    playback_fix_t fix = { .progress_ms = 42000, .sampled_at_us = g_fake.now_us + 5000000, .is_playing = true };
    TEST_ASSERT_EQUAL_UINT32(42000, spotify_progress_displayed_ms(&fix, g_clock));
}

static void test_null_fix_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, spotify_progress_displayed_ms(NULL, g_clock));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_paused_returns_progress_unchanged);
    RUN_TEST(test_playing_interpolates_forward);
    RUN_TEST(test_playing_at_exact_sample_time_is_unchanged);
    RUN_TEST(test_fix_from_the_future_does_not_go_negative);
    RUN_TEST(test_null_fix_returns_zero);
    return UNITY_END();
}
