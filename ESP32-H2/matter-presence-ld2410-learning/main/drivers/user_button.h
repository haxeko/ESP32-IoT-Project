/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdint.h>

typedef void (*user_button_cb_t)(void);

typedef struct {
    user_button_cb_t on_double_click; // two short presses in a row
    user_button_cb_t on_long_press;   // fires once when held for long_press_ms, while still held
    uint32_t long_press_ms;
} user_button_config_t;

// Active-low push button (e.g. BOOT), polled and debounced from an esp_timer.
// The callbacks run in the esp_timer task. gpio < 0 disables the button.
esp_err_t user_button_init(int gpio, const user_button_config_t *config);
