/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

typedef struct {
    int sck_pin;        // SCK (D0): SPI clock
    int sda_pin;        // SDA (D1): SPI data (MOSI)
    int res_pin;        // RES: reset
    int dc_pin;         // DC: data/command
    int cs_pin;         // CS: chip select
    int clock_hz;       // SPI clock, at most 10 MHz
    bool rotate_180;
} ssd1306_config_t;

// Sets up the SPI bus, resets the panel, clears it and turns it on.
esp_err_t ssd1306_init(const ssd1306_config_t *config);

// Drawing happens in a RAM frame buffer; ssd1306_flush() sends it to the panel.
// (0, 0) is the top left pixel.
void ssd1306_clear(void);
void ssd1306_draw_pixel(int x, int y);
void ssd1306_fill_rect(int x, int y, int width, int height);
// 5x7 font: each character takes 6 x 8 px at scale 1, 12 x 16 px at scale 2...
// ASCII and "°" only (no Vietnamese accents); other characters show as '?'.
void ssd1306_draw_text(int x, int y, const char *text, int scale);
int ssd1306_text_width(const char *text, int scale);
esp_err_t ssd1306_flush(void);
