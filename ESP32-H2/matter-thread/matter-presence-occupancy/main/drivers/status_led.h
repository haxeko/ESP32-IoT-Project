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
    STATUS_LED_COMMISSIONING, // slow blue blink: waiting to be added to a Matter controller
    STATUS_LED_PRESENCE,      // green while someone is detected
} status_led_mode_t;

// Uses the LED chosen in menuconfig (WS2812, single-colour GPIO or none);
// with none, every call is a no-op.
esp_err_t status_led_init(void);
void status_led_set_mode(status_led_mode_t mode);
void status_led_set_presence(bool present);

// Red blink while the radar sends no data (wiring problem).
void status_led_set_radar_fault(bool fault);

// Fast blink while a controller asks the device to identify itself.
void status_led_set_identify(bool identify);
