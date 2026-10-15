/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>

typedef enum {
    STATUS_LED_OFF,     // normal operation: LED dark
    STATUS_LED_PAIRING, // slow blue blink: commissioning window open, waiting to be added
} status_led_mode_t;

// Uses the LED chosen in menuconfig (WS2812, single-colour GPIO or none);
// with none, every call is a no-op.
esp_err_t status_led_init(void);
void status_led_set_mode(status_led_mode_t mode);

// Fast blink while a controller asks the device to identify itself.
void status_led_set_identify(bool identify);
