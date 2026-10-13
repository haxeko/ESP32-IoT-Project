/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/gpio.h>
#include <esp_timer.h>

#include <atomic>

#include "status_led.h"

static constexpr uint64_t kTickUs = 100 * 1000;

static int s_gpio = -1;
static bool s_active_high;
static std::atomic<status_led_mode_t> s_mode{STATUS_LED_PRESENCE};
static std::atomic<bool> s_present{false};
static std::atomic<bool> s_identify{false};
static uint32_t s_ticks;

static void led_write(bool on)
{
    gpio_set_level((gpio_num_t)s_gpio, on == s_active_high ? 1 : 0);
}

static void tick_cb(void *arg)
{
    s_ticks++;
    if (s_identify) {
        led_write(s_ticks % 2);
        return;
    }
    switch (s_mode.load()) {
    case STATUS_LED_WIFI_SETUP:
        led_write(s_ticks % 2);         // 5 Hz
        break;
    case STATUS_LED_COMMISSIONING:
        led_write((s_ticks / 10) % 2);  // 0.5 Hz
        break;
    case STATUS_LED_PRESENCE:
        led_write(s_present);
        break;
    }
}

esp_err_t status_led_init(int gpio, bool active_high)
{
    if (gpio < 0) {
        return ESP_OK;
    }
    s_gpio = gpio;
    s_active_high = active_high;
    gpio_reset_pin((gpio_num_t)gpio);
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT));
    led_write(false);

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .name = "status_led",
    };
    esp_err_t err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, kTickUs);
    }
    return err;
}

void status_led_set_mode(status_led_mode_t mode)
{
    s_mode = mode;
}

void status_led_set_presence(bool present)
{
    s_present = present;
}

void status_led_set_identify(bool identify)
{
    s_identify = identify;
}
