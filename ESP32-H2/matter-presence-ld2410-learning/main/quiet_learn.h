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

typedef enum {
    QUIET_LEARN_WAITING,   // CONFIG_LD2410_LEARN_DELAY_SEC to leave the room
    QUIET_LEARN_RECORDING, // CONFIG_LD2410_LEARN_DURATION_SEC of recording
} quiet_learn_phase_t;
// Called when each phase starts (status LED).
typedef void (*quiet_learn_phase_cb_t)(quiet_learn_phase_t phase);

typedef enum {
    QUIET_LEARN_SAVED,     // new thresholds saved in the radar
    QUIET_LEARN_CANCELLED, // cancelled, nothing written
    QUIET_LEARN_FAILED,    // no memory for the recording, or the radar did not answer, sent no reports or did
                           // not keep the thresholds
} quiet_learn_result_t;

// Engineering reports of the empty room (and the gate length while
// recording), handed over once the learning succeeded: the ESP32 presence
// engine keeps the noise of every gate from them. thresholds: what the radar
// holds then; the callback may raise some of them (above that noise), which
// are then written to the radar.
typedef void (*quiet_learn_noise_cb_t)(const ld2410_capture_t *capture, uint16_t gate_cm,
                                       ld2410_thresholds_t *thresholds);

// Runs one learning in the calling task: waits CONFIG_LD2410_LEARN_DELAY_SEC
// (time to leave the room), records CONFIG_LD2410_LEARN_DURATION_SEC of
// engineering data, then writes the new thresholds to the radar, which keeps
// them in its flash. With CONFIG_LD2410_LEARN_RADAR_AUTO, a radar that has its
// own background noise detection (newer LD2410B/C firmware) measures
// and sets its thresholds itself instead, over the same times. The recording
// (ld2410_capture_t) is allocated while the ESP32 learns. With on_noise (ESP32
// presence engine): the engineering reports of the empty room are handed to
// it when the learning succeeded, with the radar's thresholds; those it
// raises are written to the radar (QUIET_LEARN_FAILED when that fails).
// Before the radar measures itself, the ESP32 records them for noise_seconds
// (after the whole delay), then turns the engineering mode off: the radar
// measures as with the stable firmware.
// Sends radar commands, so no other task may configure the radar meanwhile.
// The callbacks are optional. gate_cm: length of one gate, for the distances
// in the log (0: unknown).
quiet_learn_result_t quiet_learn_run(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                     uint16_t gate_cm, quiet_learn_noise_cb_t on_noise = nullptr,
                                     uint32_t noise_seconds = 0);

// Writes the LD2410 factory thresholds to every gate and reads them back;
// nothing is written when the radar holds them already. The farthest gates,
// no-one duration, resolution and Bluetooth stay as they are. Sends radar
// commands like quiet_learn_run(). True when the radar holds the factory
// thresholds.
bool quiet_learn_restore_factory(void);

// Energy per gate (0-100)
typedef struct {
    uint8_t moving[LD2410_GATE_COUNT];
    uint8_t still[LD2410_GATE_COUNT];
} quiet_learn_levels_t;

// Noise of each gate in a recording: the highest energy once the
// (100 - percentile) % highest frames of the gate are left out, i.e. the
// energy that percentile % of its frames stay at or below. 100 gives the
// highest energy recorded, a gate without frames 0.
void quiet_learn_noise(const ld2410_capture_t *capture, uint8_t percentile, quiet_learn_levels_t *out);

// New threshold per gate: noise + margin, at least the factory default
// (learning only makes the radar less sensitive, where the room is noisy), at
// most the factory default + max_raise (a gate where something moved during
// the recording does not go deaf) and at most 100 (gate ignored). The still
// thresholds of gates 0 and 1 cannot be set on the LD2410 and are kept.
void quiet_learn_compute_thresholds(const quiet_learn_levels_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, uint8_t max_raise, ld2410_thresholds_t *out);
