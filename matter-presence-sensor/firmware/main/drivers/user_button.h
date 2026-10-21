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
    user_button_cb_t on_double_click;    // two short presses in a row
    user_button_cb_t on_triple_click;    // three short presses in a row
    user_button_cb_t on_quadruple_click; // four short presses in a row (nullptr: ignored like five or more)
    user_button_cb_t on_long_press;      // fires once when held for long_press_ms, while still held
    uint32_t long_press_ms;
} user_button_config_t;

// Active-low push button with the internal pull-up (BOOT, GPIO9), polled every 20 ms from an esp_timer and
// debounced over 2 samples. A click is a press of at most 500 ms; clicks are counted until the button stays
// released for more than 400 ms, so a double click fires only once it is clear that no third click follows. Five
// clicks or more do nothing; a press longer than 500 ms cancels the clicks before it. The callbacks run in the
// esp_timer task and must not block. gpio < 0: no button.
esp_err_t user_button_init(int gpio, const user_button_config_t *config);
