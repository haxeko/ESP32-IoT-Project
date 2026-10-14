// Host test of the Matter color conversions (main/color_convert.c): known colors, and the round trips that
// matter for controllers that set the color one way and read it back another way (Apple Home: hue/saturation).
#include "check.h"

#include <stdlib.h>

#include "color_convert.h"

static int near(int a, int b, int tolerance)
{
    return abs(a - b) <= tolerance;
}

static int hue_diff(int a, int b)
{
    int d = abs(a - b);
    return d > 127 ? 254 - d : d; /* hue wraps around at 254 */
}

static void test_hue_sat_to_rgb(void)
{
    rgb_t c = color_from_hue_sat(0, 254);
    CHECK(c.r == 255 && c.g == 0 && c.b == 0);
    c = color_from_hue_sat(85, 254); /* 120 deg */
    CHECK(c.r == 0 && c.g == 255 && c.b <= 3);
    c = color_from_hue_sat(169, 254); /* 240 deg */
    CHECK(c.r == 0 && c.g <= 3 && c.b == 255);
    c = color_from_hue_sat(254, 254); /* 360 deg = red again */
    CHECK(c.r == 255 && c.g == 0 && c.b == 0);
    for (int hue = 0; hue <= 254; hue += 50) {
        c = color_from_hue_sat((uint8_t)hue, 0); /* center of the color wheel: white */
        CHECK(c.r == 255 && c.g == 255 && c.b == 255);
    }
}

static void test_rgb_to_hue_sat(void)
{
    uint8_t hue = 99, sat = 99;
    color_to_hue_sat((rgb_t){255, 0, 0}, &hue, &sat);
    CHECK(hue == 0 && sat == 254);
    color_to_hue_sat((rgb_t){0, 255, 0}, &hue, &sat);
    CHECK(hue == 85 && sat == 254);
    color_to_hue_sat((rgb_t){0, 0, 255}, &hue, &sat);
    CHECK(hue == 169 && sat == 254);
    color_to_hue_sat((rgb_t){255, 255, 255}, &hue, &sat);
    CHECK(sat == 0);
    color_to_hue_sat((rgb_t){0, 0, 0}, &hue, &sat);
    CHECK(hue == 0 && sat == 0);

    /* Saturated colors survive hue/sat -> RGB -> hue/sat */
    int worst = 0;
    for (int h = 0; h < 254; h++) {
        color_to_hue_sat(color_from_hue_sat((uint8_t)h, 254), &hue, &sat);
        worst = hue_diff(hue, h) > worst ? hue_diff(hue, h) : worst;
        CHECK(sat == 254);
    }
    CHECK(worst <= 1);
}

static void test_xy(void)
{
    uint16_t x = 0, y = 0;
    /* sRGB primaries and the D65 white point (CIE 1931 x/y * 65536) */
    color_to_xy((rgb_t){255, 0, 0}, &x, &y);
    CHECK(near(x, 41943, 40) && near(y, 21627, 40)); /* 0.64, 0.33 */
    color_to_xy((rgb_t){0, 255, 0}, &x, &y);
    CHECK(near(x, 19661, 40) && near(y, 39322, 40)); /* 0.30, 0.60 */
    color_to_xy((rgb_t){0, 0, 255}, &x, &y);
    CHECK(near(x, 9830, 40) && near(y, 3932, 40)); /* 0.15, 0.06 */
    color_to_xy((rgb_t){255, 255, 255}, &x, &y);
    CHECK(near(x, 20494, 40) && near(y, 21561, 40)); /* 0.3127, 0.3290 */
    color_to_xy((rgb_t){0, 0, 0}, &x, &y);
    CHECK(near(x, 20494, 1) && near(y, 21561, 1)); /* black has no chromaticity: report the white point */

    rgb_t c = color_from_xy(20494, 21561);
    CHECK(c.r >= 250 && c.g >= 250 && c.b >= 250);
    c = color_from_xy(41943, 21627);
    CHECK(c.r == 255 && c.g <= 5 && c.b <= 5);
    c = color_from_xy(0, 0); /* invalid y = 0 must not divide by zero */
    (void)c;

    /* Values stay inside the attribute range (max 0xFEFF) */
    for (int r = 0; r <= 255; r += 51) {
        for (int g = 0; g <= 255; g += 51) {
            for (int b = 0; b <= 255; b += 51) {
                color_to_xy((rgb_t){(uint8_t)r, (uint8_t)g, (uint8_t)b}, &x, &y);
                CHECK(x <= 0xFEFF && y <= 0xFEFF);
            }
        }
    }

    /* What Apple Home sets (hue/sat) and what an XY controller reads back describe the same color */
    int worst_hue = 0, worst_sat = 0;
    for (int h = 0; h < 254; h += 3) {
        for (int s = 30; s <= 254; s += 32) {
            uint8_t h2 = 0, s2 = 0;
            color_to_xy(color_from_hue_sat((uint8_t)h, (uint8_t)s), &x, &y);
            color_to_hue_sat(color_from_xy(x, y), &h2, &s2);
            worst_hue = hue_diff(h2, h) > worst_hue ? hue_diff(h2, h) : worst_hue;
            worst_sat = abs(s2 - s) > worst_sat ? abs(s2 - s) : worst_sat;
        }
    }
    printf("  hue/sat -> XY -> hue/sat worst error: hue %d/254, saturation %d/254\n", worst_hue, worst_sat);
    CHECK(worst_hue <= 3 && worst_sat <= 3);
}

static void test_mireds(void)
{
    rgb_t c = color_from_mireds(153); /* 6500 K: white */
    CHECK(c.r >= 250 && c.g >= 240 && c.b >= 240);
    c = color_from_mireds(500); /* 2000 K: warm */
    CHECK(c.r == 255 && c.b <= 60);
    c = color_from_mireds(0); /* invalid: must not divide by zero */
    CHECK(c.b == 255);
}

int main(void)
{
    printf("color_convert:\n");
    test_hue_sat_to_rgb();
    test_rgb_to_hue_sat();
    test_xy();
    test_mireds();
    return check_report();
}
