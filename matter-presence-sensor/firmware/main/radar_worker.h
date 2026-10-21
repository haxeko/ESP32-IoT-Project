/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// The radar_cfg task: the only task that configures the LD2410, one session at a time. It runs what Home Assistant
// (radar_control.cpp) and the button ask for, without making them wait: the Range, Radar resolution and Radar
// Bluetooth changes, the empty room learning and the factory thresholds (the last choice wins). It reads the radar
// settings at boot (retried after 15 s, 30 s, 1 min... up to 5 min while the radar does not answer), and brings the
// radar's Bluetooth to the choice kept in NVS (off by default).

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "radar_settings.h"

// Modes of the "Learn empty room" select
#define RADAR_LEARN_OFF 0
#define RADAR_LEARN_ON 1
#define RADAR_LEARN_FACTORY 2 // writes the LD2410 factory thresholds, then back to Off
// "Radar resolution": the LD2410 resolution codes
#define RADAR_RESOLUTION_COARSE 0 // 0.75 m gates
#define RADAR_RESOLUTION_FINE 1   // 0.2 m gates
// "Radar Bluetooth"
#define RADAR_BLUETOOTH_OFF 0
#define RADAR_BLUETOOTH_ON 1
// "Range" modes are the farthest gate (LD2410_MIN_MAX_GATE..LD2410_MAX_MAX_GATE). The radar's own value when it is
// not one of the options: Range "Moving 4.5 m, still 3 m", Radar Bluetooth "Not available". Choosing it changes
// nothing.
#define RADAR_MODE_CUSTOM 100

typedef enum {
    RADAR_SELECT_LEARN,      // "Learn empty room": RADAR_LEARN_*
    RADAR_SELECT_RANGE,      // "Range": gate
    RADAR_SELECT_RESOLUTION, // "Radar resolution": RADAR_RESOLUTION_*
    RADAR_SELECT_BLUETOOTH,  // "Radar Bluetooth": RADAR_BLUETOOTH_*
} radar_select_t;

// How the worker shows its state (Mode Select clusters); both callbacks run on the worker task.
typedef struct {
    // Settings read from the radar; after a failed change, the ones read before.
    void (*show_settings)(const radar_settings_t *settings);
    // Mode of the "Learn empty room" select (RADAR_LEARN_*)
    void (*show_learn_mode)(uint8_t mode);
} radar_worker_view_t;

// Starts the worker (reads the Bluetooth choice from NVS first).
esp_err_t radar_worker_start(const radar_worker_view_t *view);

// A mode chosen in Home Assistant, from the Matter task (ChangeToMode): records it and wakes the worker, never
// blocks. ESP_ERR_INVALID_STATE (rejected, so Home Assistant keeps the shown value): Range, Radar resolution or
// Radar Bluetooth before the radar settings were read, or the learning or factory thresholds while the worker does
// not run. ESP_ERR_INVALID_ARG: not a mode of that select.
esp_err_t radar_worker_select(radar_select_t select, uint8_t mode);

// Triple click: the empty room learning, like "Learning" in Home Assistant. Never blocks (esp_timer task). False
// when a learning is already requested or running, or the worker does not run (the LED shows a failure).
bool radar_worker_request_learning(void);

// Four clicks: the factory thresholds, like "Factory thresholds" in Home Assistant; a learning requested or running
// is cancelled first. Never blocks. False when the worker does not run (the LED shows a failure).
bool radar_worker_request_factory_thresholds(void);
