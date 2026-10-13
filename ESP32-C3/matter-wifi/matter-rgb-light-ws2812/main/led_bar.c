#include "led_bar.h"

#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <led_strip.h>
#include <soc/soc_caps.h>

static const char *TAG = "led_bar";

#define LED_COUNT            CONFIG_LED_COUNT
#define LED_MATTER_MAX       254 /* Matter Level Control: MaxLevel */
#define IDENTIFY_HALF_PERIOD (500 * 1000) /* us */

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_identify_timer;

/* Logical state, as set by Matter */
static bool s_on;
static uint8_t s_level = LED_MATTER_MAX;
static uint8_t s_r = 255, s_g = 255, s_b = 255;

/* Identify blinking */
static bool s_identify;
static bool s_identify_phase;
static uint32_t s_identify_toggles_left; /* 0 = until stopped */

/* What is currently on the strip, to skip refreshes that would not change anything */
static struct {
    bool valid;
    uint16_t lit;
    uint8_t r, g, b;
} s_shown;

uint16_t led_bar_count_for_level(uint8_t level)
{
    if (level == 0) {
        return 0;
    }
    if (level > LED_MATTER_MAX) {
        level = LED_MATTER_MAX;
    }
    /* Round to nearest (50 % -> 15 of 30 LEDs); while on, at least one LED stays lit */
    uint16_t n = (uint16_t)(((uint32_t)level * LED_COUNT + LED_MATTER_MAX / 2) / LED_MATTER_MAX);
    return n ? n : 1;
}

static inline uint8_t scale(uint8_t c)
{
    return (uint8_t)(((uint32_t)c * CONFIG_LED_FIXED_BRIGHTNESS_PERCENT + 50) / 100);
}

/* Push the current state to the strip if the visible output changed. Caller holds s_lock. */
static void render(void)
{
    if (!s_strip) {
        return;
    }
    uint16_t lit;
    if (s_identify) {
        lit = s_identify_phase ? LED_COUNT : 0;
    } else {
        lit = s_on ? led_bar_count_for_level(s_level) : 0;
    }
    uint8_t r = scale(s_r), g = scale(s_g), b = scale(s_b);
    if (lit == 0) {
        r = g = b = 0;
    }
    if (s_shown.valid && s_shown.lit == lit && s_shown.r == r && s_shown.g == g && s_shown.b == b) {
        return;
    }

    esp_err_t err;
    if (lit == 0) {
        err = led_strip_clear(s_strip);
    } else {
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
        err = led_strip_refresh(s_strip);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(err));
        s_shown.valid = false;
        return;
    }
    s_shown.valid = true;
    s_shown.lit = lit;
    s_shown.r = r;
    s_shown.g = g;
    s_shown.b = b;
    ESP_LOGD(TAG, "%s%s, level %u -> %u/%d LEDs, RGB(%u,%u,%u)", s_on ? "ON" : "OFF", s_identify ? " (identify)" : "",
             s_level, lit, LED_COUNT, r, g, b);
}

static void identify_timer_cb(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_identify) {
        s_identify_phase = !s_identify_phase;
        if (s_identify_toggles_left && --s_identify_toggles_left == 0) {
            esp_timer_stop(s_identify_timer);
            s_identify = false;
        }
        render();
    }
    xSemaphoreGive(s_lock);
}

esp_err_t led_bar_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no mem for lock");

    const esp_timer_create_args_t timer_args = {
        .callback = identify_timer_cb,
        .name = "led_identify",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_identify_timer), TAG, "identify timer");

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
        /* ESP32-C3 RMT has no DMA: use two memory blocks so the refill ISR has twice the time
         * (~60 us instead of ~30 us of WS2812 data) before the buffer runs dry under Wi-Fi load */
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL * 2,
        .flags = {
            .with_dma = false,
        },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        s_strip = NULL;
        ESP_RETURN_ON_ERROR(err, TAG, "led_strip_new_rmt_device failed");
    }
    ESP_RETURN_ON_ERROR(led_strip_clear(s_strip), TAG, "clear failed");
    s_shown.valid = true; /* all LEDs off */
    ESP_LOGI(TAG, "WS2812 x%d on GPIO%d, fixed intensity %d%%", LED_COUNT, CONFIG_LED_GPIO,
             CONFIG_LED_FIXED_BRIGHTNESS_PERCENT);
    return ESP_OK;
}

void led_bar_set_state(bool on, uint8_t level, uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_on = on;
    s_level = level;
    s_r = r;
    s_g = g;
    s_b = b;
    render();
    xSemaphoreGive(s_lock);
}

void led_bar_set_power(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_on = on;
    render();
    xSemaphoreGive(s_lock);
}

bool led_bar_get_power(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool on = s_on;
    xSemaphoreGive(s_lock);
    return on;
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

void led_bar_identify_start(uint16_t blinks)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_identify_timer);
    s_identify = true;
    s_identify_phase = true;
    /* One blink = 0.5 s on + 0.5 s off = two toggles; after the last one the normal state is shown again */
    s_identify_toggles_left = blinks ? (uint32_t)blinks * 2 : 0;
    render();
    esp_timer_start_periodic(s_identify_timer, IDENTIFY_HALF_PERIOD);
    xSemaphoreGive(s_lock);
}

void led_bar_identify_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_identify_timer);
    s_identify = false;
    render();
    xSemaphoreGive(s_lock);
}
