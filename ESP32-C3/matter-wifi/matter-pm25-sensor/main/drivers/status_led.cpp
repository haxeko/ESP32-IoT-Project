/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// WS2812 strip used as an air quality indicator.

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>
#include <math.h>
#include <soc/soc_caps.h>

#include "status_led.h"

static const char *TAG = "status_led";

#define STATUS_LED_TRANSITION_MS    500
#define STATUS_LED_STEP_MS          20
// Same gamma correction as ESPHome lights, so the colors look the same.
#define STATUS_LED_GAMMA            2.8f

static led_strip_handle_t s_strip;
static int s_led_count;
// Current color in linear space (color * brightness), before gamma.
static float s_current[3];

static uint8_t gamma_correct(float v)
{
    if (v <= 0.0f) {
        return 0;
    }
    if (v >= 1.0f) {
        return 255;
    }
    return (uint8_t)lroundf(powf(v, STATUS_LED_GAMMA) * 255.0f);
}

static void show(const float rgb[3])
{
    const uint8_t r = gamma_correct(rgb[0]);
    const uint8_t g = gamma_correct(rgb[1]);
    const uint8_t b = gamma_correct(rgb[2]);
    for (int i = 0; i < s_led_count; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}

esp_err_t status_led_init(int gpio, int led_count)
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = led_count;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    // Use both RMT TX memory blocks (the ESP32-C3 has no RMT DMA): a 6-LED
    // frame then needs 2 refill interrupts instead of 5, each with twice the
    // time to run, so Wi-Fi interrupts are much less likely to delay a refill
    // and corrupt the colors that the strip then keeps showing.
    rmt_config.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL * 2;

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        return err;
    }
    s_led_count = led_count;
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "%d x WS2812 on GPIO%d", led_count, gpio);
    return ESP_OK;
}

void status_led_set_now(float red, float green, float blue, float brightness)
{
    if (s_strip == NULL) {
        return;
    }
    s_current[0] = red * brightness;
    s_current[1] = green * brightness;
    s_current[2] = blue * brightness;
    show(s_current);
}

void status_led_refresh(void)
{
    if (s_strip != NULL) {
        show(s_current);
    }
}

void status_led_set(float red, float green, float blue, float brightness)
{
    if (s_strip == NULL) {
        return;
    }
    const float target[3] = { red * brightness, green * brightness, blue * brightness };
    const float start[3] = { s_current[0], s_current[1], s_current[2] };
    const int steps = STATUS_LED_TRANSITION_MS / STATUS_LED_STEP_MS;
    for (int i = 1; i <= steps; i++) {
        const float t = (float)i / steps;
        for (int c = 0; c < 3; c++) {
            s_current[c] = start[c] + (target[c] - start[c]) * t;
        }
        show(s_current);
        vTaskDelay(pdMS_TO_TICKS(STATUS_LED_STEP_MS));
    }
}
