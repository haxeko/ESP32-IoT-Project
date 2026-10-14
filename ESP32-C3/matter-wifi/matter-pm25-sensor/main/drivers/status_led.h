/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>

esp_err_t status_led_init(int gpio, int led_count);

// Fades all LEDs to the given color (components and brightness in 0..1).
// Blocks for the transition time.
void status_led_set(float red, float green, float blue, float brightness);

// Sets all LEDs to the given color at once, without a fade.
void status_led_set_now(float red, float green, float blue, float brightness);

// Sends the current color again (one frame, no fade).
void status_led_refresh(void);
