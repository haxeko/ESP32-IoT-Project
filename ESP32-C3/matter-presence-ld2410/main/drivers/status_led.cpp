/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#include <stdint.h>

#include <atomic>

#if CONFIG_STATUS_LED_TYPE_WS2812
#include <led_strip.h>
#endif

#include "status_led.h"

static const char *TAG = "status_led";

static constexpr uint64_t kTickUs = 100 * 1000;
static constexpr uint32_t kResultTicks = 100; // done, failed or factory thresholds for 10 s

static std::atomic<status_led_mode_t> s_mode{STATUS_LED_OFF};
static std::atomic<status_led_learning_t> s_learning{STATUS_LED_LEARNING_NONE};
// Counts status_led_set_learning() calls: setting the same state again restarts its 10 s
static std::atomic<uint32_t> s_learning_sets{0};
static std::atomic<bool> s_identify{false};

// Only touched by the timer callback
static uint32_t s_ticks;
static status_led_learning_t s_shown_learning = STATUS_LED_LEARNING_NONE;
static uint32_t s_shown_learning_sets;
static uint32_t s_learning_ticks; // since s_shown_learning was set

typedef struct {
    uint8_t r, g, b;
} color_t;

// Colours of firmware 1.0.132 (WS2812); a single-colour LED is on for any colour
static constexpr color_t kOff = {0, 0, 0};
static constexpr color_t kBlue = {0, 0, 255};
static constexpr color_t kWhite = {255, 255, 255};
static constexpr color_t kCyan = {0, 255, 255};
static constexpr color_t kGreen = {0, 255, 0};
static constexpr color_t kRed = {255, 0, 0};
static constexpr color_t kPurple = {160, 0, 255};

static color_t led_color()
{
    if (s_identify) {
        return s_ticks % 2 != 0 ? kWhite : kOff; // 5 Hz
    }
    switch (s_shown_learning) {
    case STATUS_LED_LEARNING_WAITING:
        return kCyan;
    case STATUS_LED_LEARNING_RECORDING:
        return (s_learning_ticks / 10) % 2 != 0 ? kCyan : kOff; // 0.5 Hz, dark first
    case STATUS_LED_LEARNING_DONE:
        if (s_learning_ticks < kResultTicks) {
            return kGreen;
        }
        break;
    case STATUS_LED_LEARNING_FAILED:
        if (s_learning_ticks < kResultTicks) {
            return kRed;
        }
        break;
    case STATUS_LED_LEARNING_FACTORY:
        if (s_learning_ticks < kResultTicks) {
            return (s_learning_ticks / 2) % 2 == 0 ? kPurple : kOff; // 2.5 Hz, lit first
        }
        break;
    case STATUS_LED_LEARNING_NONE:
        break;
    }
    return s_mode == STATUS_LED_PAIRING && (s_ticks / 10) % 2 != 0 ? kBlue : kOff; // 0.5 Hz
}

#if CONFIG_STATUS_LED_TYPE_WS2812
static led_strip_handle_t s_strip;
static color_t s_last = {1, 1, 1}; // last colour sent (none yet)

static esp_err_t led_hw_init()
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = CONFIG_STATUS_LED_GPIO;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.flags.with_dma = false; // the RMT of the ESP32-C3 has no DMA
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err == ESP_OK) {
        s_last = kOff;
        err = led_strip_clear(s_strip);
    }
    return err;
}

static void led_hw_write(color_t c)
{
    if (c.r == s_last.r && c.g == s_last.g && c.b == s_last.b) {
        return;
    }
    s_last = c;
    const uint32_t k = CONFIG_STATUS_LED_BRIGHTNESS;
    led_strip_set_pixel(s_strip, 0, c.r * k / 255, c.g * k / 255, c.b * k / 255);
    led_strip_refresh(s_strip);
}
#else
// Kconfig leaves CONFIG_STATUS_LED_ACTIVE_HIGH undefined when it is off.
#if CONFIG_STATUS_LED_ACTIVE_HIGH
static constexpr bool kActiveHigh = true;
#else
static constexpr bool kActiveHigh = false;
#endif
static int s_level = -1; // last level written to the pin

static esp_err_t led_hw_init()
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_STATUS_LED_GPIO;
    gpio_reset_pin(pin);
    // Dark from the moment the output is on
    s_level = kActiveHigh ? 0 : 1;
    gpio_set_level(pin, s_level);
    return gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

static void led_hw_write(color_t c)
{
    const bool on = c.r || c.g || c.b;
    const int level = on == kActiveHigh ? 1 : 0;
    if (level != s_level) {
        s_level = level;
        gpio_set_level((gpio_num_t)CONFIG_STATUS_LED_GPIO, level);
    }
}
#endif

static void tick_cb(void *arg)
{
    s_ticks++;
    const uint32_t sets = s_learning_sets.load();
    if (sets != s_shown_learning_sets) {
        s_shown_learning_sets = sets;
        s_shown_learning = s_learning.load();
        s_learning_ticks = 0;
    } else if (s_learning_ticks < UINT32_MAX) {
        s_learning_ticks++;
    }
    led_hw_write(led_color());
}

esp_err_t status_led_init(void)
{
    esp_err_t err = led_hw_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init the status LED on GPIO%d, err:%d", CONFIG_STATUS_LED_GPIO, err);
        return err;
    }

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .name = "status_led",
    };
    err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, kTickUs);
    }
    return err;
}

void status_led_set_mode(status_led_mode_t mode)
{
    s_mode = mode;
}

void status_led_set_learning(status_led_learning_t learning)
{
    s_learning = learning;
    s_learning_sets++;
}

void status_led_set_identify(bool identify)
{
    s_identify = identify;
}
