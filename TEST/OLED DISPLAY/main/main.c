/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Test for a 0.96" SPI OLED (SSD1306, 128x64) on an ESP32-C3: lights every
// pixel, shows the display type and wiring, then information about the
// ESP32-C3, refreshed every second.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include <driver/temperature_sensor.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ssd1306.h"

static const char *TAG = "oled_test";

// Two-color modules show the top 16 rows in yellow and the others in blue:
// keep the titles in these rows.
#define HEADER_HEIGHT   16
#define LINE_HEIGHT     9

static void draw_centered(int y, const char *text, int scale)
{
    ssd1306_draw_text((SSD1306_WIDTH - ssd1306_text_width(text, scale)) / 2, y, text, scale);
}

// Line 0 is the first line under the title: label on the left, value on the right.
static void draw_line(int line, const char *label, const char *value)
{
    const int y = HEADER_HEIGHT + 2 + line * LINE_HEIGHT;
    ssd1306_draw_text(0, y, label, 1);
    ssd1306_draw_text(SSD1306_WIDTH - ssd1306_text_width(value, 1), y, value, 1);
}

static void draw_pin(int x, int y, const char *name, int gpio)
{
    char text[24];
    snprintf(text, sizeof(text), "%-3s GPIO%d", name, gpio);
    ssd1306_draw_text(x, y, text, 1);
}

// Every pixel on: shows dead pixels, and that the wiring works.
static void show_all_pixels(void)
{
    ssd1306_clear();
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, SSD1306_HEIGHT);
    ESP_ERROR_CHECK(ssd1306_flush());
}

static void show_display_info(const ssd1306_config_t *oled)
{
    ssd1306_clear();
    draw_centered(1, "OLED 0.96\"", 2);
    draw_centered(HEADER_HEIGHT + 2, "SSD1306 128x64 SPI", 1);
    draw_pin(0, 31, "SCK", oled->sck_pin);
    draw_pin(66, 31, "SDA", oled->sda_pin);
    draw_pin(0, 40, "RES", oled->res_pin);
    draw_pin(66, 40, "DC", oled->dc_pin);
    draw_pin(0, 49, "CS", oled->cs_pin);
    ESP_ERROR_CHECK(ssd1306_flush());
}

static void show_board_info(const char *revision, const char *mac, const char *flash,
                            temperature_sensor_handle_t temp_sensor)
{
    // Temperature inside the chip: a few degrees above the room temperature.
    char temperature[16] = "--";
    float celsius;
    if (temp_sensor != NULL && temperature_sensor_get_celsius(temp_sensor, &celsius) == ESP_OK) {
        snprintf(temperature, sizeof(temperature), "%ld°C", lroundf(celsius));
    }

    const uint32_t seconds = (uint32_t)(esp_timer_get_time() / 1000000);
    char uptime[16];
    snprintf(uptime, sizeof(uptime), "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
             seconds / 3600, seconds / 60 % 60, seconds % 60);

    ssd1306_clear();
    draw_centered(1, "ESP32-C3", 2);
    draw_line(0, "Revision", revision);
    draw_line(1, "MAC", mac);
    draw_line(2, "Flash", flash);
    draw_line(3, "Temp", temperature);
    draw_line(4, "Uptime", uptime);
    ESP_ERROR_CHECK(ssd1306_flush());
}

void app_main(void)
{
    const ssd1306_config_t oled = {
        .sck_pin = CONFIG_OLED_SCK_GPIO,
        .sda_pin = CONFIG_OLED_SDA_GPIO,
        .res_pin = CONFIG_OLED_RES_GPIO,
        .dc_pin = CONFIG_OLED_DC_GPIO,
        .cs_pin = CONFIG_OLED_CS_GPIO,
        .clock_hz = CONFIG_OLED_SPI_CLOCK_KHZ * 1000,
#ifdef CONFIG_OLED_ROTATE_180
        .rotate_180 = true,
#endif
    };
    // SPI only sends: the OLED never answers, so a missing or miswired
    // display cannot be detected here.
    ESP_LOGI(TAG, "OLED: SCK=GPIO%d SDA=GPIO%d RES=GPIO%d DC=GPIO%d CS=GPIO%d, SPI %d kHz",
             oled.sck_pin, oled.sda_pin, oled.res_pin, oled.dc_pin, oled.cs_pin, CONFIG_OLED_SPI_CLOCK_KHZ);
    ESP_ERROR_CHECK(ssd1306_init(&oled));

    show_all_pixels();
    vTaskDelay(pdMS_TO_TICKS(1000));
    show_display_info(&oled);
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char revision[16];
    snprintf(revision, sizeof(revision), "v%d.%d", chip.revision / 100, chip.revision % 100);

    uint8_t mac_bytes[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_bytes, ESP_MAC_WIFI_STA));
    char mac[24];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);

    // Size of the flash chip itself, whatever the size set in menuconfig.
    uint32_t flash_bytes = 0;
    esp_flash_get_physical_size(NULL, &flash_bytes);
    char flash[16];
    snprintf(flash, sizeof(flash), "%" PRIu32 " MB", flash_bytes / (1024 * 1024));

    ESP_LOGI(TAG, "ESP32-C3 %s, MAC %s, flash %s", revision, mac, flash);

    temperature_sensor_handle_t temp_sensor = NULL;
    const temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&temp_config, &temp_sensor) != ESP_OK
            || temperature_sensor_enable(temp_sensor) != ESP_OK) {
        ESP_LOGW(TAG, "Temperature sensor not available");
        temp_sensor = NULL;
    }

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        show_board_info(revision, mac, flash, temp_sensor);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
