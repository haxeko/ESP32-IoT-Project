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

typedef enum {
    BUTTON_HELD,        // every poll while held: held_ms counts up from 0 (0 = the press itself)
    BUTTON_RELEASED,    // once when released: held_ms = how long it was held
    BUTTON_CLICKS,      // a series of short presses ended: clicks = how many (2 = double press)
} button_event_type_t;

typedef struct {
    button_event_type_t type;
    uint32_t held_ms;   // BUTTON_HELD, BUTTON_RELEASED
    int clicks;         // BUTTON_CLICKS
} button_event_t;

// Called from the esp_timer task.
typedef void (*button_cb_t)(const button_event_t *event, void *user_data);

// A press shorter than BUTTON_CLICK_MAX_MS is a click. A series of clicks
// ends, with a BUTTON_CLICKS event, when no new press starts within
// BUTTON_CLICK_GAP_MS of the last release; a longer press ends it without one.
#define BUTTON_CLICK_MAX_MS 500
#define BUTTON_CLICK_GAP_MS 500

// Number of buttons button_init() accepts (main button and display button).
#define BUTTON_MAX_COUNT    2

// Push button between the GPIO and GND, read with the internal pull-up.
// Fails with ESP_ERR_INVALID_STATE if the GPIO already has a button, and
// ESP_ERR_NO_MEM after BUTTON_MAX_COUNT buttons.
esp_err_t button_init(int gpio, button_cb_t cb, void *user_data);
