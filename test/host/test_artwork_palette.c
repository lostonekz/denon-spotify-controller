/*
 * Host test for artwork_palette: dominant-colour extraction against
 * synthetic RGB565 buffers. Verified by relative channel dominance (not
 * hand-computed exact pixel values, which would be fragile arithmetic to
 * maintain) except for the two fully-predictable fallback cases
 * (monochrome image, NULL/empty buffer).
 */

#include "unity.h"

#include "artwork_palette.h"

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

#define RGB565_RED   0xF800u   /* (31, 0, 0) */
#define RGB565_BLUE  0x001Fu   /* (0, 0, 31) */
#define RGB565_BLACK 0x0000u
#define RGB565_WHITE 0xFFFFu

static uint8_t r5_of(uint16_t rgb565) { return (uint8_t)((rgb565 >> 11) & 0x1Fu); }
static uint8_t g6_of(uint16_t rgb565) { return (uint8_t)((rgb565 >> 5) & 0x3Fu); }
static uint8_t b5_of(uint16_t rgb565) { return (uint8_t)(rgb565 & 0x1Fu); }

static void fill(uint16_t *buf, size_t n, uint16_t value)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = value;
    }
}

static void test_solid_red_image_is_reddish(void)
{
    uint16_t pixels[16 * 16];
    fill(pixels, 16 * 16, RGB565_RED);
    artwork_pixel_buffer_t buf = { pixels, 16, 16 };

    uint16_t out = artwork_palette_dominant_color_rgb565(&buf);
    /* Boosted-saturation re-encoding preserves the dominant channel: red
     * should clearly outrank blue (both 5-bit, directly comparable), and
     * outrank green scaled to the same 5-bit range. */
    TEST_ASSERT_GREATER_THAN_UINT8(b5_of(out), r5_of(out));
    TEST_ASSERT_GREATER_THAN_UINT8((uint8_t)(g6_of(out) * 31u / 63u), r5_of(out));
}

static void test_solid_blue_image_is_bluish(void)
{
    uint16_t pixels[16 * 16];
    fill(pixels, 16 * 16, RGB565_BLUE);
    artwork_pixel_buffer_t buf = { pixels, 16, 16 };

    uint16_t out = artwork_palette_dominant_color_rgb565(&buf);
    TEST_ASSERT_GREATER_THAN_UINT8(r5_of(out), b5_of(out));
}

static void test_majority_color_wins_over_minority(void)
{
    uint16_t pixels[16 * 16];
    fill(pixels, 16 * 16, RGB565_RED);
    /* Overwrite a small minority with blue -- red must still dominate. */
    for (size_t i = 0; i < 40; i++) {
        pixels[i] = RGB565_BLUE;
    }
    artwork_pixel_buffer_t buf = { pixels, 16, 16 };

    uint16_t out = artwork_palette_dominant_color_rgb565(&buf);
    TEST_ASSERT_GREATER_THAN_UINT8(b5_of(out), r5_of(out));
}

static void test_solid_black_image_falls_back_to_neutral_gray(void)
{
    uint16_t pixels[16 * 16];
    fill(pixels, 16 * 16, RGB565_BLACK);
    artwork_pixel_buffer_t buf = { pixels, 16, 16 };

    /* Every pixel filtered out (v below threshold) -- exact, predictable
     * fallback: rgb888(128,128,128) round-tripped through rgb565. */
    TEST_ASSERT_EQUAL_HEX16(0x8410, artwork_palette_dominant_color_rgb565(&buf));
}

static void test_solid_white_image_falls_back_to_neutral_gray(void)
{
    uint16_t pixels[16 * 16];
    fill(pixels, 16 * 16, RGB565_WHITE);
    artwork_pixel_buffer_t buf = { pixels, 16, 16 };

    /* Every pixel filtered out (s below threshold -- white has ~zero
     * saturation) -- same fallback as the all-black case. */
    TEST_ASSERT_EQUAL_HEX16(0x8410, artwork_palette_dominant_color_rgb565(&buf));
}

static void test_null_buffer_returns_black(void)
{
    TEST_ASSERT_EQUAL_HEX16(0, artwork_palette_dominant_color_rgb565(NULL));
}

static void test_zero_dimension_returns_black(void)
{
    uint16_t pixels[4] = { RGB565_RED, RGB565_RED, RGB565_RED, RGB565_RED };
    artwork_pixel_buffer_t buf = { pixels, 0, 4 };
    TEST_ASSERT_EQUAL_HEX16(0, artwork_palette_dominant_color_rgb565(&buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_solid_red_image_is_reddish);
    RUN_TEST(test_solid_blue_image_is_bluish);
    RUN_TEST(test_majority_color_wins_over_minority);
    RUN_TEST(test_solid_black_image_falls_back_to_neutral_gray);
    RUN_TEST(test_solid_white_image_falls_back_to_neutral_gray);
    RUN_TEST(test_null_buffer_returns_black);
    RUN_TEST(test_zero_dimension_returns_black);
    return UNITY_END();
}
