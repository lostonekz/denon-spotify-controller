#include "theme_tokens.h"

#include <math.h>

/* Not M_PI: strict -std=c11 (this project's host-test flag set, not
 * -std=gnu11) does not guarantee math.h exposes it. */
#define THEME_TOKENS_PI 3.14159265358979323846

static double clamp01(double v)
{
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

static double linear_to_srgb(double c)
{
    c = clamp01(c);
    if (c <= 0.0031308) {
        return 12.92 * c;
    }
    return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

uint16_t theme_tokens_oklch_to_rgb565(oklch_color_t color)
{
    double h_rad = color.h_deg * (THEME_TOKENS_PI / 180.0);
    double a = color.c * cos(h_rad);
    double b = color.c * sin(h_rad);

    double l_ = color.l + 0.3963377774 * a + 0.2158037573 * b;
    double m_ = color.l - 0.1055613458 * a - 0.0638541728 * b;
    double s_ = color.l - 0.0894841775 * a - 1.2914855480 * b;

    double l = l_ * l_ * l_;
    double m = m_ * m_ * m_;
    double s = s_ * s_ * s_;

    double r_lin =  4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g_lin = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double b_lin = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;

    double r  = clamp01(linear_to_srgb(r_lin));
    double g  = clamp01(linear_to_srgb(g_lin));
    double bl = clamp01(linear_to_srgb(b_lin));

    uint16_t r5 = (uint16_t)(r * 31.0 + 0.5);
    uint16_t g6 = (uint16_t)(g * 63.0 + 0.5);
    uint16_t b5 = (uint16_t)(bl * 31.0 + 0.5);

    return (uint16_t)((uint16_t)(r5 << 11) | (uint16_t)(g6 << 5) | b5);
}
