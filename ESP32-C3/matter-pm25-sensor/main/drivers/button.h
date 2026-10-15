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
// (released = false, held_ms counts up from 0), then once when it is released
// (released = true, held_ms = how long it was held).
typedef void (*button_cb_t)(uint32_t held_ms, bool released, void *user_data);

// Push button between the GPIO and GND, read with the internal pull-up.
esp_err_t button_init(int gpio, button_cb_t cb, void *user_data);
