/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>

typedef void (*reset_button_cb_t)(void);

// Calls cb once when the active-low button on gpio has been held for
// hold_ms. gpio < 0 disables the button.
esp_err_t reset_button_init(int gpio, uint32_t hold_ms, reset_button_cb_t cb);
