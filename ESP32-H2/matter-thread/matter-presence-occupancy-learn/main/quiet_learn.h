/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <drivers/ld2410.h>

// Empty room learning: records the radar's background energy per distance gate
// and raises the LD2410 thresholds above it, so reflections, fans or curtains
// in an empty room stop being reported as presence.

// Polled every 100 ms during the learning: true stops it before anything is
// written to the radar.
typedef bool (*quiet_learn_cancelled_cb_t)(void);

// Runs one learning in the calling task: waits CONFIG_LD2410_LEARN_DELAY_SEC
// (time to leave the room), records CONFIG_LD2410_LEARN_DURATION_SEC of
// engineering data, then writes the new thresholds to the radar, which keeps
// them in its flash. Returns true when new thresholds were saved. Sends radar
// commands, so no other task may configure the radar meanwhile.
bool quiet_learn_run(quiet_learn_cancelled_cb_t cancelled);

// New threshold per gate: recorded noise peak + margin, at least the factory
// default (learning only makes the radar less sensitive, where the room is
// noisy) and at most CONFIG_LD2410_LEARN_MAX_THRESHOLD, so no gate is ignored. The still thresholds of gates 0 and 1
// cannot be set on the LD2410 and are kept.
void quiet_learn_compute_thresholds(const ld2410_capture_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, ld2410_thresholds_t *out);
