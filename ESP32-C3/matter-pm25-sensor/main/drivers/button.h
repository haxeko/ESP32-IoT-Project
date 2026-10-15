/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Called from the esp_timer task: every poll while the button is held
// (released = false, held_ms counts up from 0, so the first call, with
// held_ms = 0, is the press itself), then once when it is released
// (released = true, held_ms = how long it was held).
typedef void (*button_cb_t)(uint32_t held_ms, bool released, void *user_data);

// Number of buttons button_init() accepts (factory reset and display).
#define BUTTON_MAX_COUNT    2

// Push button between the GPIO and GND, read with the internal pull-up.
// Fails with ESP_ERR_INVALID_STATE if the GPIO already has a button, and
// ESP_ERR_NO_MEM after BUTTON_MAX_COUNT buttons.
esp_err_t button_init(int gpio, button_cb_t cb, void *user_data);
