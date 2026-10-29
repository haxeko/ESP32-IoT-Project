// Factory reset button: GPIO edge interrupt -> button task that times the hold.
//
// The interrupt only wakes the task. The callbacks run in the button task, never in interrupt context;
// the application does the factory reset itself (app_main.cpp).

#pragma once

#include <stdint.h>

#include <esp_err.h>

typedef void (*reset_button_cb_t)(void *user_data);

typedef struct {
    int gpio_num;                       // push button between this GPIO and GND; the internal pull-up is enabled
    uint32_t hold_ms;                   // how long the button must be held for on_hold_complete
    reset_button_cb_t on_hold_start;    // optional: button pressed (debounced), the hold timer starts
    reset_button_cb_t on_hold_cancel;   // optional: released before hold_ms, nothing happens
    reset_button_cb_t on_hold_complete; // required: held for hold_ms
    void *user_data;
} reset_button_config_t;

// Configures the GPIO (input, pull-up, interrupt on both edges) and starts the button task. A button that is
// already pressed at start-up (stuck, or still held) must be released once before it can trigger a reset, so
// a short circuit on the pin cannot cause a reset loop. The configuration is copied. Call once.
esp_err_t reset_button_init(const reset_button_config_t *config);
