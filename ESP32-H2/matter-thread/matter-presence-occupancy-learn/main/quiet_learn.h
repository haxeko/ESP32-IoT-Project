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

#include <drivers/ld2410.h>

// Empty room learning: records the radar's background energy per distance gate
// and raises the LD2410 thresholds above it, so reflections, fans or curtains
// in an empty room stop being reported as presence.

// Called from the learning task when it ends: success = new thresholds saved.
typedef void (*quiet_learn_done_cb_t)(bool success);

// Starts learning in its own task: waits CONFIG_LD2410_LEARN_DELAY_SEC (time to
// leave the room), records CONFIG_LD2410_LEARN_DURATION_SEC of engineering data,
// then writes the new thresholds to the radar, which keeps them in its flash.
// ESP_ERR_INVALID_STATE when a learning is already running.
esp_err_t quiet_learn_start(quiet_learn_done_cb_t done);

// Stops a running learning before anything is written to the radar.
void quiet_learn_cancel(void);

// New threshold per gate: recorded noise peak + margin, at least the factory
// default (learning only makes the radar less sensitive, where the room is
// noisy) and at most 100 (gate ignored). The still thresholds of gates 0 and 1
// cannot be set on the LD2410 and are kept.
void quiet_learn_compute_thresholds(const ld2410_capture_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, ld2410_thresholds_t *out);
