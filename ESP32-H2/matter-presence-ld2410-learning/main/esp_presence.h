/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32 presence engine on the device (CONFIG_PRESENCE_ESP_ENGINE): feeds presence_engine.cpp with the LD2410
// reports and the radar's thresholds, keeps the noise of the empty room in NVS and prints the diagnostics.
//
// The LD2410 reader task hands every report to a queue (never blocks: a full queue drops the report and counts
// it); the "presence_eng" task (priority 3) steps the engine, takes the commands, logs and publishes what the
// other tasks read: the occupancy for the presence task, and for the radar_cfg task whether the engineering mode
// must be turned on again and how long the radar has been silent (it then resynchronises and restarts the radar).
//
// The empty room is learned by the radar as with the stable firmware (radar_control.cpp, quiet_learn.cpp): the radar
// measures and stores its thresholds; the radar_cfg task reads them after each learning and hands them here. While
// learning, the ESP32 also records the energies of the empty room (esp_presence_learned_noise()), which keep the
// hold thresholds above the noise.
//
// NVS: namespace "esp_presence", key "noise" (blob pe_noise_t with a CRC), written only when a learning succeeded.

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "radar_settings.h"

typedef enum {
    ESP_PRESENCE_PRINT_STATUS,
    ESP_PRESENCE_PRINT_DIAG,
    ESP_PRESENCE_PRINT_THRESHOLDS,
    ESP_PRESENCE_DIAG_STREAM_ON, // per-gate diagnostics every second
    ESP_PRESENCE_DIAG_STREAM_OFF,
} esp_presence_command_t;

// Loads the noise, sets the LD2410 report hook and starts the task. Before the radar_cfg task starts.
esp_err_t esp_presence_start(void);

// Queues a command; false when the queue is full or the engine does not run.
bool esp_presence_command(esp_presence_command_t command);

// Presence task: what to report to the Occupancy Sensing cluster (pe_occupied_report()).
bool esp_presence_occupied(void);
// Presence task: the cluster's HoldTime (the absence countdown).
void esp_presence_set_hold_time(uint16_t seconds);

// radar_cfg task: radar settings after each successful session (thresholds, gate length, Range).
void esp_presence_configure(const radar_settings_t *settings);
// radar_cfg task: a configuration session or a learning starts or ends (the radar sends no reports or no energies
// meanwhile).
void esp_presence_set_radar_busy(bool busy);
// radar_cfg task: reports arrive without energies, the engineering mode must be turned on.
bool esp_presence_needs_engineering(void);
// radar_cfg task: time since the last valid report (since boot before the first one), 0 while reports arrive.
uint32_t esp_presence_silent_ms(void);
// radar_cfg task, from quiet_learn_run() once a learning succeeded: the engineering reports of the empty room
// (gate length while recording). The noise of every channel is stored in NVS, then used.
void esp_presence_learned_noise(const ld2410_capture_t *capture, uint16_t gate_cm);

// Console on the configured ESP-IDF console (USB-Serial-JTAG or UART): LEARN_EMPTY, LEARN_CANCEL,
// FACTORY_THRESHOLDS, STATUS, DIAG, DIAG_STREAM, THRESHOLDS, help (CONFIG_PRESENCE_ENGINE_CONSOLE). With the Matter
// shell (CONFIG_ENABLE_CHIP_SHELL), added to its console; otherwise on a REPL of its own. After esp_matter::start().
esp_err_t esp_presence_console_start(void);
