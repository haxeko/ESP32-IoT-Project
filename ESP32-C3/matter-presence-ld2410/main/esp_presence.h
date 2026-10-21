/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32 presence engine on the device (CONFIG_PRESENCE_ESP_ENGINE): feeds presence_engine.cpp with the LD2410
// reports, keeps its learned profile in NVS and runs its commands.
//
// The LD2410 reader task hands every report to a queue (never blocks: a full queue drops the report and counts
// it); the "presence_eng" task (priority 3) steps the engine, takes the commands, logs and publishes what the
// other tasks read: the occupancy for the presence task, and for the radar_cfg task whether the engineering mode
// must be turned on again and how long the radar has been silent (it then resynchronises and restarts the radar).
//
// Commands (never block, from any task): LEARN_EMPTY ("Learning" in Home Assistant, 3 clicks, console), cancel
// ("Off"), back to the built-in thresholds ("Factory thresholds", 4 clicks), and the console prints (status,
// per-gate diagnostics, learned profile).
//
// NVS: namespace "esp_presence", key "profile" (blob pe_profile_t with a CRC), written only when a learning
// succeeded, erased by the profile reset. Nothing else is written.

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "radar_settings.h"

typedef enum {
    ESP_PRESENCE_LEARN,         // LEARN_EMPTY
    ESP_PRESENCE_LEARN_CANCEL,  // stops a learning, the old profile stays
    ESP_PRESENCE_PROFILE_RESET, // erases the learned profile: built-in per-gate thresholds
    ESP_PRESENCE_PRINT_STATUS,
    ESP_PRESENCE_PRINT_DIAG,
    ESP_PRESENCE_PRINT_PROFILE,
    ESP_PRESENCE_DIAG_STREAM_ON, // per-gate diagnostics every second
    ESP_PRESENCE_DIAG_STREAM_OFF,
} esp_presence_command_t;

// Loads the profile, sets the LD2410 report hook and starts the task. Before the radar_cfg task starts.
esp_err_t esp_presence_start(void);

// Queues a command; false when the queue is full or the engine does not run.
bool esp_presence_command(esp_presence_command_t command);

// Presence task: what to report to the Occupancy Sensing cluster (pe_occupied_report()).
bool esp_presence_occupied(void);
// Presence task: the cluster's HoldTime (the absence countdown).
void esp_presence_set_hold_time(uint16_t seconds);

// radar_cfg task: radar settings after each successful session (gate length, Range).
void esp_presence_configure(const radar_settings_t *settings);
// radar_cfg task: a configuration session starts or ends (the radar sends no reports meanwhile).
void esp_presence_set_radar_busy(bool busy);
// radar_cfg task: reports arrive without energies, the engineering mode must be turned on.
bool esp_presence_needs_engineering(void);
// radar_cfg task: time since the last valid report (since boot before the first one), 0 while reports arrive.
uint32_t esp_presence_silent_ms(void);

// How the "Learn empty room" select shows the learning (RADAR_LEARN_*); set by the radar_cfg task at its start.
void esp_presence_set_learn_view(void (*show_learn_mode)(uint8_t mode));

// Console on the configured ESP-IDF console (USB-Serial-JTAG or UART): LEARN_EMPTY, LEARN_CANCEL, STATUS, DIAG,
// DIAG_STREAM, PROFILE, PROFILE_RESET, help (CONFIG_PRESENCE_ENGINE_CONSOLE).
esp_err_t esp_presence_console_start(void);
