#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

/** Matter Color Control hue/saturation (0..254 each) -> full-scale RGB. */
rgb_t color_from_hue_sat(uint8_t hue, uint8_t sat);

/** Matter Color Control CurrentX/CurrentY (0..65279, CIE 1931 x/y * 65536) -> full-scale RGB. */
rgb_t color_from_xy(uint16_t x, uint16_t y);

/** Matter Color Control ColorTemperatureMireds -> full-scale RGB (white point of that temperature). */
rgb_t color_from_mireds(uint16_t mireds);

/** sRGB color -> Matter CurrentHue / CurrentSaturation (0..254 each). */
void color_to_hue_sat(rgb_t c, uint8_t *hue, uint8_t *sat);

/** sRGB color -> Matter CurrentX / CurrentY (CIE 1931 x/y * 65536, at most 65279). */
void color_to_xy(rgb_t c, uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
