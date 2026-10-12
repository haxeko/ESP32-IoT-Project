/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "ui/fonts.h"

#define SH1106_WIDTH    128
#define SH1106_HEIGHT   64

// Same meaning as ESPHome's TextAlign: the (x, y) anchor point of the text box.
typedef enum {
    TEXT_ALIGN_TOP_LEFT,
    TEXT_ALIGN_TOP_CENTER,
    TEXT_ALIGN_TOP_RIGHT,
    TEXT_ALIGN_CENTER,
} text_align_t;

// Can be called again after a failure to retry (e.g. the panel powered up late).
esp_err_t sh1106_init(int sda_pin, int scl_pin, uint8_t address, bool rotate_180);

// Panel on/off (display sleep). The frame buffer is kept.
esp_err_t sh1106_set_power(bool on);

// Drawing happens in a RAM frame buffer; sh1106_flush() sends it to the panel.
void sh1106_clear(void);
void sh1106_draw_pixel(int x, int y);
void sh1106_draw_hline(int x0, int x1, int y);
// 1-bit bitmap, rows packed MSB first and padded to a whole byte.
void sh1106_draw_bitmap(int x, int y, int width, int height, const uint8_t *data);
void sh1106_draw_text(int x, int y, const font_t *font, text_align_t align, const char *utf8);
esp_err_t sh1106_flush(void);
