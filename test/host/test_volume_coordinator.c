/*
 * Host test for main/volume_coordinator.c: every device_is_avr x
 * is_restricted x supports_volume combination, and confidence-flag
 * correctness independent of the retarget decision itself.
 */

#include "unity.h"

#include "volume_coordinator.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static playback_state_t make_playback(device_is_avr_t is_avr, bool restricted, bool supports_volume)
{
    playback_state_t p;
    memset(&p, 0, sizeof(p));
    p.device_is_avr           = is_avr;
    p.device_is_restricted    = restricted;
    p.device_supports_volume  = supports_volume;
    return p;
}

static void test_unknown_defaults_to_denon_not_confident(void)
{
    playback_state_t p = make_playback(DEVICE_IS_AVR_UNKNOWN, false, true);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_FALSE(r.confident);
}

static void test_yes_targets_denon_confidently(void)
{
    playback_state_t p = make_playback(DEVICE_IS_AVR_YES, false, true);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_no_unrestricted_supports_volume_targets_playback(void)
{
    playback_state_t p = make_playback(DEVICE_IS_AVR_NO, false, true);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_PLAYBACK, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_no_but_restricted_falls_back_to_denon_confidently(void)
{
    /* Confirmed non-AVR, but the Web API can't control it at all -- not a
     * viable retarget candidate. Still a confident identification. */
    playback_state_t p = make_playback(DEVICE_IS_AVR_NO, true, true);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_no_but_no_volume_support_falls_back_to_denon_confidently(void)
{
    playback_state_t p = make_playback(DEVICE_IS_AVR_NO, false, false);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_no_restricted_and_no_volume_support_falls_back_to_denon(void)
{
    playback_state_t p = make_playback(DEVICE_IS_AVR_NO, true, false);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_yes_with_restricted_and_no_volume_support_still_denon(void)
{
    /* device_is_avr == YES always resolves to Denon regardless of the
     * other two flags -- they only matter for the NO case. */
    playback_state_t p = make_playback(DEVICE_IS_AVR_YES, true, false);
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, &p);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_TRUE(r.confident);
}

static void test_null_playback_defaults_to_denon_not_confident(void)
{
    volume_target_resolution_t r = resolve_volume_target(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(VOLUME_TARGET_DENON, r.domain);
    TEST_ASSERT_FALSE(r.confident);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unknown_defaults_to_denon_not_confident);
    RUN_TEST(test_yes_targets_denon_confidently);
    RUN_TEST(test_no_unrestricted_supports_volume_targets_playback);
    RUN_TEST(test_no_but_restricted_falls_back_to_denon_confidently);
    RUN_TEST(test_no_but_no_volume_support_falls_back_to_denon_confidently);
    RUN_TEST(test_no_restricted_and_no_volume_support_falls_back_to_denon);
    RUN_TEST(test_yes_with_restricted_and_no_volume_support_still_denon);
    RUN_TEST(test_null_playback_defaults_to_denon_not_confident);
    return UNITY_END();
}
