#include "led_bar.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <led_strip.h>

static const char *TAG = "led_bar";

#define LED_COUNT        CONFIG_LED_COUNT
#define LED_MATTER_MAX   254 /* Matter Level Control: MaxLevel */

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;

static bool s_on;
static uint8_t s_level = LED_MATTER_MAX;
static uint8_t s_r = 255, s_g = 255, s_b = 255;

uint16_t led_bar_count_for_level(uint8_t level)
{
    if (level == 0) {
        return 0;
    }
    if (level > LED_MATTER_MAX) {
        level = LED_MATTER_MAX;
    }
    /* Round to nearest (50 % -> 32 of 64 LEDs); while on, at least one LED stays lit */
    uint16_t n = (uint16_t)(((uint32_t)level * LED_COUNT + LED_MATTER_MAX / 2) / LED_MATTER_MAX);
    return n ? n : 1;
}

static inline uint8_t scale(uint8_t c)
{
    return (uint8_t)(((uint32_t)c * CONFIG_LED_FIXED_BRIGHTNESS_PERCENT + 50) / 100);
}

/* Push the current state to the strip. Caller holds s_lock. */
static void render(void)
{
    if (!s_strip) {
        return;
    }
    uint16_t lit = s_on ? led_bar_count_for_level(s_level) : 0;
    uint8_t r = scale(s_r), g = scale(s_g), b = scale(s_b);

    for (uint16_t i = 0; i < LED_COUNT; i++) {
#if CONFIG_LED_FILL_REVERSE
        uint16_t idx = LED_COUNT - 1 - i;
#else
        uint16_t idx = i;
#endif
        if (i < lit) {
            led_strip_set_pixel(s_strip, idx, r, g, b);
        } else {
            led_strip_set_pixel(s_strip, idx, 0, 0, 0);
        }
    }
    led_strip_refresh(s_strip);
    ESP_LOGI(TAG, "%s, level %u (%u%%) -> %u/%d LEDs, RGB(%u,%u,%u) @%d%%", s_on ? "ON" : "OFF", s_level,
             (unsigned)((s_level * 100 + LED_MATTER_MAX / 2) / LED_MATTER_MAX), lit, LED_COUNT, s_r, s_g, s_b,
             CONFIG_LED_FIXED_BRIGHTNESS_PERCENT);
}

esp_err_t led_bar_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false, /* ESP32-S2 RMT has no DMA */
        },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return err;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 x%d on GPIO%d, fixed intensity %d%%", LED_COUNT, CONFIG_LED_GPIO,
             CONFIG_LED_FIXED_BRIGHTNESS_PERCENT);
    return ESP_OK;
}

void led_bar_set_power(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_on = on;
    render();
    xSemaphoreGive(s_lock);
}

void led_bar_set_level(uint8_t level)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_level = level;
    render();
    xSemaphoreGive(s_lock);
}

void led_bar_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_r = r;
    s_g = g;
    s_b = b;
    render();
    xSemaphoreGive(s_lock);
}
