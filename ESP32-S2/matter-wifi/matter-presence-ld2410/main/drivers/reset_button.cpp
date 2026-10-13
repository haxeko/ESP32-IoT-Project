/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "reset_button.h"

static const char *TAG = "reset_button";

static constexpr uint32_t kPollMs = 50;

static int s_gpio;
static uint32_t s_hold_ms;
static reset_button_cb_t s_cb;
static uint32_t s_pressed_ms;
static bool s_armed;

static void poll_cb(void *arg)
{
    if (gpio_get_level((gpio_num_t)s_gpio) != 0) {
        s_pressed_ms = 0;
        s_armed = true;
        return;
    }
    // A pin that reads low from boot on (held button, wiring, strapping) must
    // never trigger a reset loop: only count presses that start after release.
    if (!s_armed) {
        return;
    }
    if (s_pressed_ms == 0) {
        ESP_LOGI(TAG, "Button pressed, hold for %" PRIu32 " s to factory reset", s_hold_ms / 1000);
    }
    const bool fire = s_pressed_ms < s_hold_ms && s_pressed_ms + kPollMs >= s_hold_ms;
    s_pressed_ms += kPollMs;
    if (fire) {
        s_cb();
    }
}

esp_err_t reset_button_init(int gpio, uint32_t hold_ms, reset_button_cb_t cb)
{
    if (gpio < 0) {
        return ESP_OK;
    }
    s_gpio = gpio;
    s_hold_ms = hold_ms;
    s_cb = cb;

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = poll_cb,
        .name = "reset_button",
    };
    err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, kPollMs * 1000);
    }
    return err;
}
