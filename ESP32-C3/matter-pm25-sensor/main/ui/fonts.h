/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1-bit glyph, rows packed MSB first and padded to a whole byte.
typedef struct {
    uint32_t codepoint;
    uint16_t offset;        // index into font_t::bitmap
    uint8_t width;
    uint8_t height;
    int8_t x_offset;        // from the pen position
    int8_t y_offset;        // from the baseline (negative = above)
    uint8_t advance;
} font_glyph_t;

typedef struct {
    const font_glyph_t *glyphs;     // sorted by codepoint
    uint16_t glyph_count;
    const uint8_t *bitmap;
    uint8_t ascent;
    uint8_t descent;
} font_t;

extern const font_t font_small;     // Roboto Regular 12 px (+ Vietnamese)
extern const font_t font_medium;    // Roboto Bold 16 px
extern const font_t font_large;     // Roboto Bold 28 px
extern const font_t font_value;     // Roboto Bold 24 px, digits and '-' only

#ifdef __cplusplus
}
#endif
