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

#include "status_led.h"

static const char *TAG = "status_led";

static constexpr uint64_t kTickUs = 100 * 1000;
static constexpr uint32_t kResultTicks = 100; // done, failed or factory thresholds for 10 s

// Kconfig leaves CONFIG_STATUS_LED_ACTIVE_HIGH undefined when it is off.
#if CONFIG_STATUS_LED_ACTIVE_HIGH
static constexpr bool kActiveHigh = true;
#else
static constexpr bool kActiveHigh = false;
#endif

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
static int s_level = -1;          // last level written to the pin

static bool led_on()
{
    if (s_identify) {
        return s_ticks % 2 != 0; // 5 Hz
    }
    switch (s_shown_learning) {
    case STATUS_LED_LEARNING_WAITING:
        return true;
    case STATUS_LED_LEARNING_RECORDING:
        return (s_learning_ticks / 10) % 2 != 0; // 0.5 Hz, dark first
    case STATUS_LED_LEARNING_DONE:
    case STATUS_LED_LEARNING_FAILED:
        if (s_learning_ticks < kResultTicks) {
            return true;
        }
        break;
    case STATUS_LED_LEARNING_FACTORY:
        if (s_learning_ticks < kResultTicks) {
            return (s_learning_ticks / 2) % 2 == 0; // 2.5 Hz, lit first
        }
        break;
    case STATUS_LED_LEARNING_NONE:
        break;
    }
    return s_mode == STATUS_LED_PAIRING && (s_ticks / 10) % 2 != 0; // 0.5 Hz
}

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
    const int level = led_on() == kActiveHigh ? 1 : 0;
    if (level != s_level) {
        s_level = level;
        gpio_set_level((gpio_num_t)CONFIG_STATUS_LED_GPIO, level);
    }
}

esp_err_t status_led_init(void)
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_STATUS_LED_GPIO;
    gpio_reset_pin(pin);
    // Dark from the moment the output is on
    s_level = kActiveHigh ? 0 : 1;
    gpio_set_level(pin, s_level);
    esp_err_t err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
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
