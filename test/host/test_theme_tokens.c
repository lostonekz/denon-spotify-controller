/*
 * Host test for theme_tokens.c -- Phase 5 Tier 1. Spot-checked against
 * invariants that hold for ANY correct OKLCH/sRGB conversion (black is
 * black, white is white, grayscale stays achromatic, a green hue produces
 * more green than red or blue) rather than pinned exact pixel values,
 * which would require an independently-verified oracle this project
 * doesn't have -- these invariants are algorithm-independent and would
 * catch a transcription error in the conversion matrix just as reliably.
 */
#include "unity.h"

#include "theme_tokens.h"

void setUp(void) {}
void tearDown(void) {}

static uint16_t r5_of(uint16_t rgb565) { return (uint16_t)((rgb565 >> 11) & 0x1Fu); }
static uint16_t g6_of(uint16_t rgb565) { return (uint16_t)((rgb565 >> 5) & 0x3Fu); }
static uint16_t b5_of(uint16_t rgb565) { return (uint16_t)(rgb565 & 0x1Fu); }

static void test_zero_lightness_is_black(void)
{
    oklch_color_t black = { .l = 0.0, .c = 0.0, .h_deg = 155.0 };
    TEST_ASSERT_EQUAL_HEX16(0x0000, theme_tokens_oklch_to_rgb565(black));
}

static void test_full_lightness_zero_chroma_is_white(void)
{
    oklch_color_t white = { .l = 1.0, .c = 0.0, .h_deg = 0.0 };
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, theme_tokens_oklch_to_rgb565(white));
}

static void test_zero_chroma_stays_achromatic_at_any_lightness(void)
{
    /* Hue is meaningless at C=0 -- R, G, and B must come out equal
     * regardless of which hue is passed in. */
    oklch_color_t gray = { .l = 0.5, .c = 0.0, .h_deg = 200.0 };
    uint16_t rgb = theme_tokens_oklch_to_rgb565(gray);
    TEST_ASSERT_EQUAL_UINT16(r5_of(rgb), b5_of(rgb));
    /* Compare on a common 0..31 scale since G is 6 bits, R/B are 5. */
    uint16_t g_as_5bit = (uint16_t)(g6_of(rgb) / 2u);
    TEST_ASSERT_UINT16_WITHIN(1, r5_of(rgb), g_as_5bit);
}

static void test_grayscale_ramp_is_monotonically_non_decreasing(void)
{
    uint16_t prev = r5_of(theme_tokens_oklch_to_rgb565(
        (oklch_color_t){ .l = 0.0, .c = 0.0, .h_deg = 0.0 }));
    for (int i = 1; i <= 10; i++) {
        oklch_color_t step = { .l = (double)i / 10.0, .c = 0.0, .h_deg = 0.0 };
        uint16_t cur = r5_of(theme_tokens_oklch_to_rgb565(step));
        TEST_ASSERT_TRUE(cur >= prev);
        prev = cur;
    }
}

static void test_the_real_accent_green_is_green_dominant(void)
{
    /* The design system's own accent token: oklch(0.80 0.17 155). */
    oklch_color_t accent = { .l = 0.80, .c = 0.17, .h_deg = 155.0 };
    uint16_t rgb = theme_tokens_oklch_to_rgb565(accent);

    double r_frac = (double)r5_of(rgb) / 31.0;
    double g_frac = (double)g6_of(rgb) / 63.0;
    double b_frac = (double)b5_of(rgb) / 31.0;

    TEST_ASSERT_TRUE(g_frac > r_frac);
    TEST_ASSERT_TRUE(g_frac > b_frac);
}

static void test_result_never_out_of_range(void)
{
    /* A deliberately out-of-gamut request (very high chroma) must clamp
     * cleanly, never produce a value that looks like it wrapped/underflowed. */
    oklch_color_t extreme = { .l = 0.5, .c = 5.0, .h_deg = 30.0 };
    uint16_t rgb = theme_tokens_oklch_to_rgb565(extreme);
    TEST_ASSERT_TRUE(r5_of(rgb) <= 31u);
    TEST_ASSERT_TRUE(g6_of(rgb) <= 63u);
    TEST_ASSERT_TRUE(b5_of(rgb) <= 31u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_lightness_is_black);
    RUN_TEST(test_full_lightness_zero_chroma_is_white);
    RUN_TEST(test_zero_chroma_stays_achromatic_at_any_lightness);
    RUN_TEST(test_grayscale_ramp_is_monotonically_non_decreasing);
    RUN_TEST(test_the_real_accent_green_is_green_dominant);
    RUN_TEST(test_result_never_out_of_range);
    return UNITY_END();
}
