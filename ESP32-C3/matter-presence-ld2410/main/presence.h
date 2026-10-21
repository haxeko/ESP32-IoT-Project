/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Radar -> occupancy: the presence task polls the LD2410 every 100 ms and hands each change to the Occupancy
// Sensing cluster, which applies HoldTime itself (occupied at once, unoccupied after HoldTime).

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include <drivers/ld2410.h>

// Someone is there: a moving or still target (target states 0-3 only, see ld2410_target_t), and when max_cm is not
// 0 only targets at max_cm or closer.
bool presence_from_target(const ld2410_target_t *t, uint16_t max_cm);

// Delivers a presence change to the cluster. False when it could not (Matter not started): retried at the next poll.
typedef bool (*presence_report_cb_t)(bool present);

// ESP32 presence engine (CONFIG_PRESENCE_ESP_ENGINE): reads the Occupancy Sensing HoldTime (seconds) into *out,
// false when it cannot yet. Polled every second by the presence task, which hands it to the engine.
typedef bool (*presence_hold_time_cb_t)(uint16_t *out);
void presence_set_hold_time_source(presence_hold_time_cb_t read_hold_time);

// Starts the presence task (priority 2, above the Matter task). A radar that sends nothing valid for
// CONFIG_RADAR_TIMEOUT_SEC (counted from boot before the first report) is logged as an error and counts as no
// one, so the room does not stay occupied forever. Only changes are reported, the first state once.
esp_err_t presence_start(presence_report_cb_t report);
