/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Push button polled from an esp_timer: no interrupt, so contact bounce or
// noise on a long wire cannot flood the CPU, and a level must last
// BUTTON_DEBOUNCE_SAMPLES polls before it counts.

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "button.h"

static const char *TAG = "button";

#define BUTTON_POLL_MS              50
#define BUTTON_DEBOUNCE_SAMPLES     2

static gpio_num_t s_gpio;
static button_cb_t s_cb;
static void *s_user_data;
static bool s_pressed;                  // debounced state
static int s_changed_samples;           // polls in a row that disagree with s_pressed
static int64_t s_pressed_since_ms;

static void poll_button(void *arg)
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool pressed = gpio_get_level(s_gpio) == 0;

    if (pressed == s_pressed) {
        s_changed_samples = 0;
    } else if (++s_changed_samples >= BUTTON_DEBOUNCE_SAMPLES) {
        s_changed_samples = 0;
        s_pressed = pressed;
        if (pressed) {
            s_pressed_since_ms = now_ms;
        } else {
            s_cb((uint32_t)(now_ms - s_pressed_since_ms), true, s_user_data);
        }
    }

    if (s_pressed) {
        s_cb((uint32_t)(now_ms - s_pressed_since_ms), false, s_user_data);
    }
}

esp_err_t button_init(int gpio, button_cb_t cb, void *user_data)
{
    if (!GPIO_IS_VALID_GPIO(gpio) || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_gpio = (gpio_num_t)gpio;
    s_cb = cb;
    s_user_data = user_data;

    gpio_config_t io_config = {};
    io_config.pin_bit_mask = 1ULL << gpio;
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pull_up_en = GPIO_PULLUP_ENABLE;
    io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_config.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_config);
    if (err != ESP_OK) {
        return err;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = poll_button;
    timer_args.name = "button";
    esp_timer_handle_t timer;
    err = esp_timer_create(&timer_args, &timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_start_periodic(timer, BUTTON_POLL_MS * 1000);
    if (err != ESP_OK) {
        esp_timer_delete(timer);
        return err;
    }
    ESP_LOGI(TAG, "Button on GPIO%d", gpio);
    return ESP_OK;
}
