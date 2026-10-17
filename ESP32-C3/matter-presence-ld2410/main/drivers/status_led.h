/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Single-colour status LED (the blue LED of the ESP32-C3 SuperMini, GPIO8, on when low), driven from a 100 ms
// esp_timer. Priority: identify, then the empty room learning and factory thresholds, then the mode.

#pragma once

#include <esp_err.h>
#include <stdbool.h>

typedef enum {
    STATUS_LED_OFF,     // normal operation: LED dark
    STATUS_LED_PAIRING, // slow blink (1 s on, 1 s off): commissioning window open, waiting to be added
} status_led_mode_t;

// Empty room learning and factory thresholds, shown instead of the mode while they last. The colours of the
// ESP32-H2 firmware (RGB LED) are given for reference.
typedef enum {
    STATUS_LED_LEARNING_NONE,      // not learning: the mode
    STATUS_LED_LEARNING_WAITING,   // on (cyan): leave the room, the recording starts soon
    STATUS_LED_LEARNING_RECORDING, // slow blink, dark first (cyan): recording the empty room
    STATUS_LED_LEARNING_DONE,      // on for 10 s (green): new thresholds saved, then the mode
    STATUS_LED_LEARNING_FAILED,    // on for 10 s (red): the learning or the factory thresholds failed, then the mode
    STATUS_LED_LEARNING_FACTORY,   // 2.5 Hz blink for 10 s (purple): factory thresholds written, then the mode
} status_led_learning_t;

// Pin and polarity from menuconfig (CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_ACTIVE_HIGH). The LED stays dark
// while it starts.
esp_err_t status_led_init(void);
void status_led_set_mode(status_led_mode_t mode);
// Setting a 10 s state again (a second failure) restarts its 10 s.
void status_led_set_learning(status_led_learning_t learning);
// 5 Hz blink while a controller asks the device to identify itself, over everything else.
void status_led_set_identify(bool identify);
