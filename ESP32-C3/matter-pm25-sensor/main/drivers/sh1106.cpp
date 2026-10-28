/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// SH1106 or SSD1306 128x64 OLED over I2C, 1-bit frame buffer and bitmap font
// rendering. Both take the same commands here; only the RAM column offset
// differs (CONFIG_OLED_CONTROLLER_SSD1306).

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "sh1106.h"

static const char *TAG = "sh1106";

#define SH1106_I2C_FREQ_HZ      400000
#define SH1106_I2C_TIMEOUT_MS   100
#if CONFIG_OLED_CONTROLLER_SSD1306
// The SSD1306 has exactly 128 columns: an offset would shift the image and
// wrap the last columns around to the first ones (garbage on one edge).
#define SH1106_COLUMN_OFFSET    0
#else
// The SH1106 has 132 columns of RAM; a 128 px panel is centered on them.
#define SH1106_COLUMN_OFFSET    2
#endif
#define SH1106_PAGES            (SH1106_HEIGHT / 8)

static i2c_master_dev_handle_t s_dev;
static bool s_rotate_180;
// Page-major layout, as in the controller RAM: byte = 8 vertical pixels.
static uint8_t s_frame[SH1106_PAGES][SH1106_WIDTH];

static esp_err_t send_commands(const uint8_t *cmds, size_t len)
{
    uint8_t buf[1 + 32];
    if (len > sizeof(buf) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = 0x00;  // control byte: command stream
    memcpy(&buf[1], cmds, len);
    return i2c_master_transmit(s_dev, buf, len + 1, SH1106_I2C_TIMEOUT_MS);
}

// Panel settings, without display on/off. Sent at init and again by
// sh1106_set_power(true): a panel that lost power forgets them, and an
// SSD1306-based module then stays dark without its charge pump command.
static esp_err_t send_settings()
{
    const uint8_t cmds[] = {
        0xD5, 0x80,                     // clock divide ratio / oscillator frequency
        0xA8, 0x3F,                     // multiplex ratio: 64
        0xD3, 0x00,                     // display offset
        0x40,                           // start line 0
        0x8D, 0x14,                     // charge pump on, as ESPHome does: needed by SSD1306-based
                                        // "SH1106" modules, ignored by a real SH1106 (its DC-DC is on at reset)
        (uint8_t)(s_rotate_180 ? 0xA0 : 0xA1),  // segment remap
        (uint8_t)(s_rotate_180 ? 0xC0 : 0xC8),  // COM scan direction
        0xDA, 0x12,                     // COM pins configuration
        0x81, 0xFF,                     // contrast 100%
        0xD9, 0xF1,                     // pre-charge period
        0xDB, 0x40,                     // VCOMH deselect level
        0xA4,                           // display follows RAM
        0xA6,                           // normal (not inverted)
    };
    return send_commands(cmds, sizeof(cmds));
}

// The panel may need some time after power-up before it answers on I2C.
#define SH1106_INIT_ATTEMPTS    5
#define SH1106_INIT_RETRY_MS    100

static void log_bus_scan(i2c_master_bus_handle_t bus)
{
    bool found = false;
    for (uint16_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            ESP_LOGW(TAG, "I2C device found at 0x%02X", addr);
            found = true;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "No I2C device answers: check the wiring and power of the I2C bus");
    }
}

esp_err_t sh1106_init(i2c_master_bus_handle_t bus, uint8_t address, bool rotate_180)
{
    esp_err_t err;
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    // Added once, so that a retry also recovers from a failure here.
    if (s_dev == NULL) {
        i2c_device_config_t dev_config = {};
        dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_config.device_address = address;
        dev_config.scl_speed_hz = SH1106_I2C_FREQ_HZ;
        err = i2c_master_bus_add_device(bus, &dev_config, &s_dev);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_rotate_180 = rotate_180;
    for (int attempt = 1; attempt <= SH1106_INIT_ATTEMPTS; attempt++) {
        err = sh1106_set_power(false);
        if (err == ESP_OK) {
            err = send_settings();
        }
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(SH1106_INIT_RETRY_MS));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No response from OLED at 0x%02X: %s", address, esp_err_to_name(err));
        static bool s_scanned;
        if (!s_scanned) {   // only once, the UI keeps retrying the init
            s_scanned = true;
            log_bus_scan(bus);
        }
        return err;
    }
    sh1106_clear();
    err = sh1106_flush();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OLED ready at 0x%02X", address);
    }
    return err;
}

esp_err_t sh1106_set_power(bool on)
{
    if (on) {
        const esp_err_t err = send_settings();
        if (err != ESP_OK) {
            return err;
        }
    }
    const uint8_t cmd = on ? 0xAF : 0xAE;
    return send_commands(&cmd, 1);
}

void sh1106_clear(void)
{
    memset(s_frame, 0, sizeof(s_frame));
}

void sh1106_draw_pixel(int x, int y)
{
    if (x < 0 || x >= SH1106_WIDTH || y < 0 || y >= SH1106_HEIGHT) {
        return;
    }
    s_frame[y / 8][x] |= 1 << (y % 8);
}

void sh1106_draw_hline(int x0, int x1, int y)
{
    for (int x = x0; x <= x1; x++) {
        sh1106_draw_pixel(x, y);
    }
}

void sh1106_draw_bitmap(int x, int y, int width, int height, const uint8_t *data)
{
    const int bytes_per_row = (width + 7) / 8;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            if (data[row * bytes_per_row + col / 8] & (0x80 >> (col % 8))) {
                sh1106_draw_pixel(x + col, y + row);
            }
        }
    }
}

esp_err_t sh1106_flush(void)
{
    uint8_t buf[1 + SH1106_WIDTH];
    buf[0] = 0x40;  // control byte: data stream
    for (int page = 0; page < SH1106_PAGES; page++) {
        const uint8_t addr_cmds[] = {
            (uint8_t)(0xB0 | page),                         // page address
            (uint8_t)(SH1106_COLUMN_OFFSET & 0x0F),         // 0x00-0x0F: column address, low nibble
            (uint8_t)(0x10 | (SH1106_COLUMN_OFFSET >> 4)),  // 0x10-0x1F: column address, high nibble
        };
        esp_err_t err = send_commands(addr_cmds, sizeof(addr_cmds));
        if (err != ESP_OK) {
            return err;
        }
        memcpy(&buf[1], s_frame[page], SH1106_WIDTH);
        err = i2c_master_transmit(s_dev, buf, sizeof(buf), SH1106_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

// Decodes one UTF-8 code point and advances *s. Invalid bytes map to '?'.
static uint32_t utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return '?';
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += 1 + extra;
    return cp;
}

static const font_glyph_t *find_glyph(const font_t *font, uint32_t cp)
{
    int lo = 0;
    int hi = font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font->glyphs[mid].codepoint == cp) {
            return &font->glyphs[mid];
        }
        if (font->glyphs[mid].codepoint < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return cp == '?' ? NULL : find_glyph(font, '?');
}

static int text_width(const font_t *font, const char *utf8)
{
    int width = 0;
    while (*utf8) {
        const font_glyph_t *g = find_glyph(font, utf8_next(&utf8));
        if (g) {
            width += g->advance;
        }
    }
    return width;
}

static void draw_glyph(int pen_x, int baseline, const font_t *font, const font_glyph_t *g)
{
    // Glyphs use the same 1-bit format as the bitmaps.
    sh1106_draw_bitmap(pen_x + g->x_offset, baseline + g->y_offset, g->width, g->height, &font->bitmap[g->offset]);
}

void sh1106_draw_text(int x, int y, const font_t *font, text_align_t align, const char *utf8)
{
    const int width = text_width(font, utf8);
    const int height = font->ascent + font->descent;
    switch (align) {
    case TEXT_ALIGN_TOP_CENTER:
        x -= width / 2;
        break;
    case TEXT_ALIGN_TOP_RIGHT:
        x -= width;
        break;
    case TEXT_ALIGN_CENTER:
        x -= width / 2;
        y -= height / 2;
        break;
    case TEXT_ALIGN_TOP_LEFT:
    default:
        break;
    }

    const int baseline = y + font->ascent;
    while (*utf8) {
        const font_glyph_t *g = find_glyph(font, utf8_next(&utf8));
        if (g) {
            draw_glyph(x, baseline, font, g);
            x += g->advance;
        }
    }
}
