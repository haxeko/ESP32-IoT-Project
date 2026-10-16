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

// Empty room learning, shown instead of the mode while it lasts
typedef enum {
    STATUS_LED_LEARNING_NONE,      // not learning: the mode
    STATUS_LED_LEARNING_WAITING,   // cyan: leave the room, the recording starts soon
    STATUS_LED_LEARNING_RECORDING, // slow cyan blink: recording the empty room
    STATUS_LED_LEARNING_DONE,      // green for 10 s (new thresholds saved), then the mode
    STATUS_LED_LEARNING_FAILED,    // red for 10 s (the learning failed), then the mode
} status_led_learning_t;

// Uses the LED chosen in menuconfig (WS2812, single-colour GPIO or none);
// with none, every call is a no-op. A single-colour LED shows every colour
// as on.
esp_err_t status_led_init(void);
void status_led_set_mode(status_led_mode_t mode);
void status_led_set_learning(status_led_learning_t learning);

// Fast blink while a controller asks the device to identify itself, over
// everything else.
void status_led_set_identify(bool identify);
