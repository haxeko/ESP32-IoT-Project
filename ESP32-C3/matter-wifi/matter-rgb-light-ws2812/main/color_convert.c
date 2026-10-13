#include "color_convert.h"

#include <math.h>

static uint8_t clamp_u8(float v)
{
    if (v <= 0.0f) {
        return 0;
    }
    if (v >= 255.0f) {
        return 255;
    }
    return (uint8_t)(v + 0.5f);
}

rgb_t color_from_hue_sat(uint8_t hue, uint8_t sat)
{
    /* HSV with V = 1: Matter hue 0..254 = 0..360 deg, saturation 0..254 = 0..1 */
    float h = (float)hue * 360.0f / 254.0f;
    float s = (float)sat / 254.0f;
    if (h >= 360.0f) {
        h = 0.0f;
    }

    float c = s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    switch ((int)hp) {
    case 0: r1 = c; g1 = x; break;
    case 1: r1 = x; g1 = c; break;
    case 2: g1 = c; b1 = x; break;
    case 3: g1 = x; b1 = c; break;
    case 4: r1 = x; b1 = c; break;
    default: r1 = c; b1 = x; break;
    }
    float m = 1.0f - c;
    rgb_t out = {clamp_u8((r1 + m) * 255.0f), clamp_u8((g1 + m) * 255.0f), clamp_u8((b1 + m) * 255.0f)};
    return out;
}

static float srgb_gamma(float v)
{
    if (v <= 0.0f) {
        return 0.0f;
    }
    return v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

rgb_t color_from_xy(uint16_t cx, uint16_t cy)
{
    float x = (float)cx / 65536.0f;
    float y = (float)cy / 65536.0f;
    if (y < 0.0001f) {
        y = 0.0001f;
    }
    /* CIE xyY (Y = 1) -> XYZ -> linear sRGB (D65) */
    float X = x / y;
    float Y = 1.0f;
    float Z = (1.0f - x - y) / y;

    float r = 3.2406f * X - 1.5372f * Y - 0.4986f * Z;
    float g = -0.9689f * X + 1.8758f * Y + 0.0415f * Z;
    float b = 0.0557f * X - 0.2040f * Y + 1.0570f * Z;

    /* Out of gamut: drop the negative part, then normalise so the strongest channel is full scale */
    r = r < 0 ? 0 : r;
    g = g < 0 ? 0 : g;
    b = b < 0 ? 0 : b;
    float mx = fmaxf(r, fmaxf(g, b));
    if (mx <= 0.0f) {
        rgb_t white = {255, 255, 255};
        return white;
    }
    r = srgb_gamma(r / mx);
    g = srgb_gamma(g / mx);
    b = srgb_gamma(b / mx);
    rgb_t out = {clamp_u8(r * 255.0f), clamp_u8(g * 255.0f), clamp_u8(b * 255.0f)};
    return out;
}

rgb_t color_from_mireds(uint16_t mireds)
{
    if (mireds == 0) {
        mireds = 1;
    }
    /* Tanner Helland black-body approximation, valid 1000 K .. 40000 K */
    float t = 1000000.0f / (float)mireds / 100.0f;
    float r, g, b;

    if (t <= 66.0f) {
        r = 255.0f;
        g = 99.4708025861f * logf(t) - 161.1195681661f;
    } else {
        r = 329.698727446f * powf(t - 60.0f, -0.1332047592f);
        g = 288.1221695283f * powf(t - 60.0f, -0.0755148492f);
    }
    if (t >= 66.0f) {
        b = 255.0f;
    } else if (t <= 19.0f) {
        b = 0.0f;
    } else {
        b = 138.5177312231f * logf(t - 10.0f) - 305.0447927307f;
    }
    rgb_t out = {clamp_u8(r), clamp_u8(g), clamp_u8(b)};
    return out;
}
