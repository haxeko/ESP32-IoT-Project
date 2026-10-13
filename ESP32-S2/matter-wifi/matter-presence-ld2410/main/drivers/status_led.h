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
    STATUS_LED_WIFI_SETUP,    // fast blink: setup access point is open
    STATUS_LED_COMMISSIONING, // slow blink: waiting to be added to a Matter controller
    STATUS_LED_PRESENCE,      // on while someone is detected
    STATUS_LED_CODE,          // N short blinks, pause, repeat (see status_led_set_code)
} status_led_mode_t;

// gpio < 0 disables the LED; every other call is then a no-op.
esp_err_t status_led_init(int gpio, bool active_high);
void status_led_set_mode(status_led_mode_t mode);
void status_led_set_presence(bool present);

// Switches to STATUS_LED_CODE with the given number of blinks per cycle.
void status_led_set_code(int blinks);

// Fast blink while a controller asks the device to identify itself.
void status_led_set_identify(bool identify);
