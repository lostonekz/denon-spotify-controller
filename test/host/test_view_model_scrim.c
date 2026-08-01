/*
 * Host test for view_model_scrim.c -- Phase 5 plan §5.
 */
#include "unity.h"

#include "view_model_scrim.h"

void setUp(void) {}
void tearDown(void) {}

static void test_dark_cover_stays_at_baseline(void)
{
    TEST_ASSERT_EQUAL_UINT8(VIEW_MODEL_SCRIM_BASELINE_OPA,
                              view_model_scrim_opacity_for_luma(0));
}

static void test_at_midpoint_still_baseline(void)
{
    TEST_ASSERT_EQUAL_UINT8(VIEW_MODEL_SCRIM_BASELINE_OPA,
                              view_model_scrim_opacity_for_luma(VIEW_MODEL_SCRIM_LUMA_MIDPOINT));
}

static void test_brightest_possible_cover_reaches_ceiling(void)
{
    TEST_ASSERT_EQUAL_UINT8(VIEW_MODEL_SCRIM_CEILING_OPA,
                              view_model_scrim_opacity_for_luma(255));
}

static void test_never_exceeds_ceiling(void)
{
    uint8_t opa = view_model_scrim_opacity_for_luma(255);
    TEST_ASSERT_TRUE(opa <= VIEW_MODEL_SCRIM_CEILING_OPA);
}

static void test_never_drops_below_baseline(void)
{
    for (int luma = 0; luma <= 255; luma++) {
        uint8_t opa = view_model_scrim_opacity_for_luma((uint8_t)luma);
        TEST_ASSERT_TRUE(opa >= VIEW_MODEL_SCRIM_BASELINE_OPA);
    }
}

static void test_monotonically_non_decreasing_across_the_full_range(void)
{
    /* The design system's own pale-cover concern only gets fixed if a
     * lighter cover NEVER produces a less-opaque scrim than a darker
     * one -- a single regression anywhere in the range would let some
     * pale cover slip through with an under-tuned scrim. */
    uint8_t prev = view_model_scrim_opacity_for_luma(0);
    for (int luma = 1; luma <= 255; luma++) {
        uint8_t cur = view_model_scrim_opacity_for_luma((uint8_t)luma);
        TEST_ASSERT_TRUE(cur >= prev);
        prev = cur;
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dark_cover_stays_at_baseline);
    RUN_TEST(test_at_midpoint_still_baseline);
    RUN_TEST(test_brightest_possible_cover_reaches_ceiling);
    RUN_TEST(test_never_exceeds_ceiling);
    RUN_TEST(test_never_drops_below_baseline);
    RUN_TEST(test_monotonically_non_decreasing_across_the_full_range);
    return UNITY_END();
}
