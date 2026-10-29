// Reed switch driver: GPIO edge interrupt -> debounce task -> stable contact state.
//
// The driver knows nothing about Matter. It reports the debounced state through a callback; the
// application decides what to do with it (app_main.cpp updates the Boolean State cluster).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

typedef enum {
    CONTACT_STATE_CLOSED = 0, // magnet at the reed switch: door or window closed
    CONTACT_STATE_OPEN = 1,   // magnet away from the reed switch: door or window open
} contact_state_t;

// Called from the driver task (never from the ISR) each time the debounced state changes.
// Keep it short and non-blocking: it delays the next debounce cycle.
typedef void (*contact_sensor_cb_t)(contact_state_t state, void *user_data);

typedef struct {
    int gpio_num;                  // reed switch between this GPIO and GND; the internal pull-up is enabled
    bool closed_level_low;         // true: a closed contact reads LOW (CONFIG_CONTACT_ACTIVE_LOW)
    uint32_t debounce_ms;          // quiet time after the last edge before the level is accepted
    contact_sensor_cb_t on_change; // required
    void *user_data;
} contact_sensor_config_t;

// GPIO level -> contact state, the one place where the wiring polarity is applied.
contact_state_t reed_level_to_contact_state(int level, bool closed_level_low);

const char *contact_state_to_str(contact_state_t state);

// Configures the GPIO (input, pull-up, interrupt on both edges), reads the real initial state of the pin
// (two equal readings one debounce time apart) and starts the debounce task. The configuration is copied.
// Call once. The initial state is not reported through on_change: read it with contact_sensor_get_state().
esp_err_t contact_sensor_init(const contact_sensor_config_t *config);

// Last debounced state. Safe from any task.
contact_state_t contact_sensor_get_state(void);

// Raw GPIO level right now (not debounced), for logs.
int contact_sensor_get_level(void);
