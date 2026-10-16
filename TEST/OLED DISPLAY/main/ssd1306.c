/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// SSD1306 128x64 OLED over 4-wire SPI, 1-bit frame buffer and 5x7 text.

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "font5x7.h"
#include "ssd1306.h"

static const char *TAG = "ssd1306";

#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)
// 1 px of space after each character
#define CHAR_ADVANCE    (FONT5X7_WIDTH + 1)

static spi_device_handle_t s_spi;
static int s_dc_pin;
// Same layout as the panel RAM: 8 pages of 8 rows; each byte is a column of
// 8 pixels of a page, bit 0 at the top.
static uint8_t s_frame[SSD1306_PAGES][SSD1306_WIDTH];

// DC low: command bytes, including their parameters. DC high: pixel data.
static esp_err_t send(bool data, const uint8_t *bytes, size_t len)
{
    gpio_set_level(s_dc_pin, data);
    spi_transaction_t t = {
        .length = len * 8,  // in bits
        .tx_buffer = bytes,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

esp_err_t ssd1306_init(const ssd1306_config_t *config)
{
    s_dc_pin = config->dc_pin;
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << config->dc_pin) | (1ULL << config->res_pin),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "DC/RES pins");

    const spi_bus_config_t bus = {
        .mosi_io_num = config->sda_pin,
        .miso_io_num = -1,  // the panel only receives
        .sclk_io_num = config->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(s_frame),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    const spi_device_interface_config_t device = {
        .mode = 0,
        .clock_speed_hz = config->clock_hz,
        .spics_io_num = config->cs_pin,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device, &s_spi), TAG, "SPI device");

    // Hardware reset: RES low for at least 3 us.
    gpio_set_level(config->res_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(config->res_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    const uint8_t init_cmds[] = {
        0xAE,                               // display off
        0xD5, 0x80,                         // clock divide ratio / oscillator frequency
        0xA8, SSD1306_HEIGHT - 1,           // multiplex ratio: 64 rows
        0xD3, 0x00,                         // display offset: none
        0x40,                               // display start line: 0
        0x8D, 0x14,                         // charge pump on (the module has no external panel supply)
        0x20, 0x00,                         // horizontal addressing mode, used by ssd1306_flush()
        config->rotate_180 ? 0xA0 : 0xA1,   // segment remap (left/right)
        config->rotate_180 ? 0xC0 : 0xC8,   // COM scan direction (top/bottom)
        0xDA, 0x12,                         // COM pins configuration for 128x64
        0x81, 0xCF,                         // contrast
        0xD9, 0xF1,                         // pre-charge period
        0xDB, 0x40,                         // VCOMH deselect level
        0xA4,                               // display follows RAM
        0xA6,                               // normal, not inverted
        0x2E,                               // scrolling off
    };
    ESP_RETURN_ON_ERROR(send(false, init_cmds, sizeof(init_cmds)), TAG, "init commands");

    // Clear the panel RAM before turning the display on.
    ssd1306_clear();
    ESP_RETURN_ON_ERROR(ssd1306_flush(), TAG, "clear");
    const uint8_t display_on = 0xAF;
    return send(false, &display_on, 1);
}

void ssd1306_clear(void)
{
    memset(s_frame, 0, sizeof(s_frame));
}

void ssd1306_draw_pixel(int x, int y)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    s_frame[y / 8][x] |= 1 << (y % 8);
}

void ssd1306_fill_rect(int x, int y, int width, int height)
{
    for (int row = y; row < y + height; row++) {
        for (int col = x; col < x + width; col++) {
            ssd1306_draw_pixel(col, row);
        }
    }
}

// Returns the font columns of the next character of a UTF-8 string and
// advances *text past it.
static const uint8_t *next_glyph(const char **text)
{
    const uint8_t *s = (const uint8_t *)*text;
    if (s[0] == 0xC2 && s[1] == 0xB0) {    // "°" in UTF-8
        *text += 2;
        return font5x7_degree;
    }
    *text += 1;
    if (s[0] >= FONT5X7_FIRST && s[0] <= FONT5X7_LAST) {
        return font5x7[s[0] - FONT5X7_FIRST];
    }
    // Not in the font: skip the rest of a multi-byte character, show one '?'.
    while ((**text & 0xC0) == 0x80) {
        *text += 1;
    }
    return font5x7['?' - FONT5X7_FIRST];
}

void ssd1306_draw_text(int x, int y, const char *text, int scale)
{
    while (*text) {
        const uint8_t *columns = next_glyph(&text);
        for (int col = 0; col < FONT5X7_WIDTH; col++) {
            for (int row = 0; row < FONT5X7_HEIGHT; row++) {
                if (columns[col] & (1 << row)) {
                    ssd1306_fill_rect(x + col * scale, y + row * scale, scale, scale);
                }
            }
        }
        x += CHAR_ADVANCE * scale;
    }
}

int ssd1306_text_width(const char *text, int scale)
{
    int count = 0;
    while (*text) {
        next_glyph(&text);
        count++;
    }
    // No space after the last character.
    return count > 0 ? (count * CHAR_ADVANCE - 1) * scale : 0;
}

esp_err_t ssd1306_flush(void)
{
    const uint8_t whole_screen[] = {
        0x21, 0, SSD1306_WIDTH - 1,         // column range
        0x22, 0, SSD1306_PAGES - 1,         // page range
    };
    ESP_RETURN_ON_ERROR(send(false, whole_screen, sizeof(whole_screen)), TAG, "flush");
    return send(true, &s_frame[0][0], sizeof(s_frame));
}
