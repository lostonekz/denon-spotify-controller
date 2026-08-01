/*
 * Host test for view_model_format.c -- Phase 5 Tier 1.
 */
#include <string.h>

#include "unity.h"

#include "view_model_format.h"

void setUp(void) {}
void tearDown(void) {}

static void test_mmss_zero(void)
{
    char buf[16];
    view_model_format_mmss(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0:00", buf);
}

static void test_mmss_just_under_a_minute(void)
{
    char buf[16];
    view_model_format_mmss(59999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0:59", buf);
}

static void test_mmss_exactly_a_minute(void)
{
    char buf[16];
    view_model_format_mmss(60000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1:00", buf);
}

static void test_mmss_multi_digit_minutes(void)
{
    char buf[16];
    view_model_format_mmss(187000, buf, sizeof(buf));   /* UI_ARCHITECTURE.md's own example */
    TEST_ASSERT_EQUAL_STRING("3:07", buf);
}

static void test_remaining_normal_case(void)
{
    char buf[16];
    view_model_format_remaining_mmss(43000, 127000, buf, sizeof(buf));   /* 84s left -> 1:24 */
    TEST_ASSERT_EQUAL_STRING("-1:24", buf);
}

static void test_remaining_clamps_to_zero_when_elapsed_overshoots_duration(void)
{
    char buf[16];
    /* A brief interpolation overshoot near track end must not show as a
     * jump to a huge (wrapped) or negative-looking number. */
    view_model_format_remaining_mmss(130000, 127000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("-0:00", buf);
}

static void test_remaining_exactly_at_duration(void)
{
    char buf[16];
    view_model_format_remaining_mmss(127000, 127000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("-0:00", buf);
}

static void test_half_db_whole_value(void)
{
    char buf[16];
    view_model_format_half_db(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.0", buf);
}

static void test_half_db_half_step(void)
{
    char buf[16];
    view_model_format_half_db(1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.5", buf);
}

static void test_half_db_typical_value(void)
{
    char buf[16];
    view_model_format_half_db(105, buf, sizeof(buf));   /* denon_codec.h's own doc example */
    TEST_ASSERT_EQUAL_STRING("52.5", buf);
}

static void test_half_db_max_value(void)
{
    char buf[16];
    view_model_format_half_db(197, buf, sizeof(buf));   /* DENON_MV_HALF_DB_MAX, 98.5 dB */
    TEST_ASSERT_EQUAL_STRING("98.5", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mmss_zero);
    RUN_TEST(test_mmss_just_under_a_minute);
    RUN_TEST(test_mmss_exactly_a_minute);
    RUN_TEST(test_mmss_multi_digit_minutes);
    RUN_TEST(test_remaining_normal_case);
    RUN_TEST(test_remaining_clamps_to_zero_when_elapsed_overshoots_duration);
    RUN_TEST(test_remaining_exactly_at_duration);
    RUN_TEST(test_half_db_whole_value);
    RUN_TEST(test_half_db_half_step);
    RUN_TEST(test_half_db_typical_value);
    RUN_TEST(test_half_db_max_value);
    return UNITY_END();
}
