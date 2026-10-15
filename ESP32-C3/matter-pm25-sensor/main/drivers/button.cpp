/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Push buttons, each polled from its own esp_timer: no interrupt, so contact
// bounce or noise on a long wire cannot flood the CPU, and a level must last
// BUTTON_DEBOUNCE_SAMPLES polls before it counts.

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "button.h"

static const char *TAG = "button";

#define BUTTON_POLL_MS              50
#define BUTTON_DEBOUNCE_SAMPLES     2

typedef struct {
    gpio_num_t gpio;
    button_cb_t cb;
    void *user_data;
    bool pressed;               // debounced state
    int changed_samples;        // polls in a row that disagree with pressed
    int64_t pressed_since_ms;
} button_t;

// A slot is filled before its timer starts, and then only used by that timer.
static button_t s_buttons[BUTTON_MAX_COUNT];
static int s_button_count;

static void poll_button(void *arg)
{
    button_t *b = (button_t *)arg;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool pressed = gpio_get_level(b->gpio) == 0;

    if (pressed == b->pressed) {
        b->changed_samples = 0;
    } else if (++b->changed_samples >= BUTTON_DEBOUNCE_SAMPLES) {
        b->changed_samples = 0;
        b->pressed = pressed;
        if (pressed) {
            b->pressed_since_ms = now_ms;
        } else {
            b->cb((uint32_t)(now_ms - b->pressed_since_ms), true, b->user_data);
        }
    }

    if (b->pressed) {
        b->cb((uint32_t)(now_ms - b->pressed_since_ms), false, b->user_data);
    }
}

esp_err_t button_init(int gpio, button_cb_t cb, void *user_data)
{
    if (!GPIO_IS_VALID_GPIO(gpio) || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < s_button_count; i++) {
        if (s_buttons[i].gpio == gpio) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (s_button_count >= BUTTON_MAX_COUNT) {
        return ESP_ERR_NO_MEM;
    }
    button_t *b = &s_buttons[s_button_count];
    *b = {};
    b->gpio = (gpio_num_t)gpio;
    b->cb = cb;
    b->user_data = user_data;

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
    timer_args.arg = b;
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
    s_button_count++;
    ESP_LOGI(TAG, "Button on GPIO%d", gpio);
    return ESP_OK;
}
