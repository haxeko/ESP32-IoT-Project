/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <atomic>

#include "status_led.h"

static const char *TAG = "status_led";

static constexpr uint64_t kTickUs = 100 * 1000;
static constexpr uint32_t kTickMs = kTickUs / 1000;

static std::atomic<bool> s_pairing{false};
static std::atomic<bool> s_sensor_error{false};
static std::atomic<bool> s_identify{false};
static std::atomic<uint32_t> s_identify_ticks{0}; // left of a TriggerEffect

static int s_gpio = -1;
static bool s_active_high;
// Only touched by the timer callback
static uint32_t s_ticks;
static int s_level = -1; // last level written to the pin

static bool led_on()
{
    if (s_identify || s_identify_ticks > 0) {
        return s_ticks % 2 != 0; // 5 Hz
    }
    if (s_pairing) {
        return (s_ticks / 10) % 2 != 0; // 0.5 Hz
    }
    if (s_sensor_error) {
        return s_ticks % 20 == 0; // 100 ms every 2 s
    }
    return false;
}

static void tick_cb(void *arg)
{
    s_ticks++;
    uint32_t left = s_identify_ticks.load();
    while (left > 0 && !s_identify_ticks.compare_exchange_weak(left, left - 1)) {
    }
    const int level = led_on() == s_active_high ? 1 : 0;
    if (level != s_level) {
        s_level = level;
        gpio_set_level((gpio_num_t)s_gpio, level);
    }
}

esp_err_t status_led_init(int gpio, bool active_high)
{
    if (gpio < 0) {
        return ESP_OK;
    }
    s_gpio = gpio;
    s_active_high = active_high;
    const gpio_num_t pin = (gpio_num_t)gpio;
    gpio_reset_pin(pin);
    // Dark from the moment the output is on
    s_level = active_high ? 0 : 1;
    gpio_set_level(pin, s_level);
    esp_err_t err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Status LED on GPIO%d not started, err:%d", gpio, err);
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

void status_led_set_pairing(bool pairing)
{
    s_pairing = pairing;
}

void status_led_set_sensor_error(bool error)
{
    s_sensor_error = error;
}

void status_led_set_identify(bool identify)
{
    s_identify = identify;
}

void status_led_identify_for(uint32_t ms)
{
    s_identify_ticks = (ms + kTickMs - 1) / kTickMs;
}
