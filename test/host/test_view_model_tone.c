/*
 * Host test for view_model_tone.c -- Phase 5 plan §4. Exhaustive over all
 * 4 real link_status_t values, not just a couple of samples: this mapping
 * has no default: case on purpose, so a future 5th value fails to compile
 * here rather than silently picking a tone.
 */
#include "unity.h"

#include "view_model_tone.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ok_is_live(void)
{
    TEST_ASSERT_EQUAL(TONE_LIVE, view_model_tone_from_link_status(LINK_STATUS_OK));
}

static void test_stale_is_stale(void)
{
    TEST_ASSERT_EQUAL(TONE_STALE, view_model_tone_from_link_status(LINK_STATUS_STALE));
}

static void test_reconnecting_is_stale_not_offline(void)
{
    /* The one non-obvious mapping: a retry in flight is reported as STALE
     * (data aging, system still trying), never OFFLINE (system given up). */
    TEST_ASSERT_EQUAL(TONE_STALE, view_model_tone_from_link_status(LINK_STATUS_RECONNECTING));
}

static void test_unavailable_is_offline(void)
{
    TEST_ASSERT_EQUAL(TONE_OFFLINE, view_model_tone_from_link_status(LINK_STATUS_UNAVAILABLE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ok_is_live);
    RUN_TEST(test_stale_is_stale);
    RUN_TEST(test_reconnecting_is_stale_not_offline);
    RUN_TEST(test_unavailable_is_offline);
    return UNITY_END();
}
