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

#include <atomic>

#include "status_led.h"

#if CONFIG_STATUS_LED_TYPE_WS2812
#include <led_strip.h>
#endif

static std::atomic<status_led_mode_t> s_mode{STATUS_LED_OFF};
static std::atomic<status_led_learning_t> s_learning{STATUS_LED_LEARNING_NONE};
// Counts status_led_set_learning() calls: setting the same state again (a
// second failure) restarts its 10 s
static std::atomic<uint32_t> s_learning_sets{0};
static std::atomic<bool> s_identify{false};

#if !CONFIG_STATUS_LED_TYPE_NONE
static const char *TAG = "status_led";

static constexpr uint64_t kTickUs = 100 * 1000;
static constexpr uint32_t kLearningResultTicks = 100; // green, red or purple for 10 s

typedef struct {
    uint8_t r, g, b;
} color_t;

static constexpr color_t kOff = {0, 0, 0};
static constexpr color_t kBlue = {0, 0, 255};
static constexpr color_t kWhite = {255, 255, 255};
static constexpr color_t kCyan = {0, 255, 255};
static constexpr color_t kGreen = {0, 255, 0};
static constexpr color_t kRed = {255, 0, 0};
static constexpr color_t kPurple = {160, 0, 255};

// Only touched by the status_led timer callback
static uint32_t s_ticks;
static status_led_learning_t s_shown_learning = STATUS_LED_LEARNING_NONE;
static uint32_t s_shown_learning_sets;
static uint32_t s_learning_ticks; // since s_shown_learning was set
#endif

#if CONFIG_STATUS_LED_TYPE_WS2812
static led_strip_handle_t s_strip;

static esp_err_t led_hw_init()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_STATUS_LED_GPIO,
        .max_leds = 1,
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
            .with_dma = false, /* the RMT of ESP32-H2 and ESP32-C3 has no DMA */
        },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err == ESP_OK) {
        led_strip_clear(s_strip);
    }
    return err;
}

static void led_hw_write(color_t c)
{
    const uint32_t k = CONFIG_STATUS_LED_BRIGHTNESS;
    led_strip_set_pixel(s_strip, 0, c.r * k / 255, c.g * k / 255, c.b * k / 255);
    led_strip_refresh(s_strip);
}

#elif CONFIG_STATUS_LED_TYPE_GPIO

// Kconfig leaves CONFIG_STATUS_LED_ACTIVE_HIGH undefined when it is off.
#if CONFIG_STATUS_LED_ACTIVE_HIGH
static constexpr bool kActiveHigh = true;
#else
static constexpr bool kActiveHigh = false;
#endif

static esp_err_t led_hw_init()
{
    gpio_reset_pin((gpio_num_t)CONFIG_STATUS_LED_GPIO);
    gpio_set_level((gpio_num_t)CONFIG_STATUS_LED_GPIO, kActiveHigh ? 0 : 1); // dark once the output is on
    return gpio_set_direction((gpio_num_t)CONFIG_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
}

static void led_hw_write(color_t c)
{
    const bool on = c.r || c.g || c.b;
    gpio_set_level((gpio_num_t)CONFIG_STATUS_LED_GPIO, on == kActiveHigh ? 1 : 0);
}

#endif

#if !CONFIG_STATUS_LED_TYPE_NONE
static color_t current_color()
{
    if (s_identify) {
        return (s_ticks % 2) ? kWhite : kOff;            // 5 Hz
    }
    switch (s_shown_learning) {
    case STATUS_LED_LEARNING_WAITING:
        return kCyan;
    case STATUS_LED_LEARNING_RECORDING:
        return ((s_learning_ticks / 10) % 2) ? kCyan : kOff; // 0.5 Hz, dark first
    case STATUS_LED_LEARNING_DONE:
        if (s_learning_ticks < kLearningResultTicks) {
            return kGreen;
        }
        break;
    case STATUS_LED_LEARNING_FAILED:
        if (s_learning_ticks < kLearningResultTicks) {
            return kRed;
        }
        break;
    case STATUS_LED_LEARNING_FACTORY:
        if (s_learning_ticks < kLearningResultTicks) {
            return ((s_learning_ticks / 2) % 2) ? kOff : kPurple; // 2.5 Hz, lit first
        }
        break;
    case STATUS_LED_LEARNING_NONE:
        break;
    }
    switch (s_mode.load()) {
    case STATUS_LED_PAIRING:
        return ((s_ticks / 10) % 2) ? kBlue : kOff;      // 0.5 Hz
    case STATUS_LED_OFF:
        return kOff;
    }
    return kOff;
}

static void tick_cb(void *arg)
{
    static color_t s_last = {1, 1, 1}; // forces the first write
    s_ticks++;
    const uint32_t sets = s_learning_sets.load();
    if (sets != s_shown_learning_sets) {
        s_shown_learning_sets = sets;
        s_shown_learning = s_learning.load();
        s_learning_ticks = 0;
    } else if (s_learning_ticks < UINT32_MAX) {
        s_learning_ticks++;
    }
    const color_t c = current_color();
    if (c.r != s_last.r || c.g != s_last.g || c.b != s_last.b) {
        s_last = c;
        led_hw_write(c);
    }
}
#endif

esp_err_t status_led_init(void)
{
#if CONFIG_STATUS_LED_TYPE_NONE
    return ESP_OK;
#else
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
#endif
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
