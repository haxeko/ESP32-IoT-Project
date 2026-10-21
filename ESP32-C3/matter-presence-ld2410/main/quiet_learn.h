/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Empty room learning: the radar measures the background energy of every distance gate and the LD2410 thresholds
// are raised above it, so reflections, fans or curtains in an empty room stop being reported as presence. Also the
// "Factory thresholds" that undo it.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <drivers/ld2410.h>

// Polled every 100 ms during the learning: true stops it before anything is written to the radar.
typedef bool (*quiet_learn_cancelled_cb_t)(void);

typedef enum {
    QUIET_LEARN_WAITING,   // CONFIG_LD2410_LEARN_DELAY_SEC to leave the room
    QUIET_LEARN_RECORDING, // CONFIG_LD2410_LEARN_DURATION_SEC of recording
} quiet_learn_phase_t;
// Called when each phase starts (status LED).
typedef void (*quiet_learn_phase_cb_t)(quiet_learn_phase_t phase);

typedef enum {
    QUIET_LEARN_SAVED,     // new thresholds stored in the radar
    QUIET_LEARN_CANCELLED, // cancelled, nothing written
    QUIET_LEARN_FAILED,    // the radar did not answer, sent no reports, failed its own measurement or did not keep the
                           // thresholds, or no memory for the recording
} quiet_learn_result_t;

// Engineering reports of the empty room (and the gate length while recording), handed over before quiet_learn_run()
// returns QUIET_LEARN_SAVED: the ESP32 presence engine keeps the noise of every gate from them.
typedef void (*quiet_learn_noise_cb_t)(const ld2410_capture_t *capture, uint16_t gate_cm);

// Runs one learning in the calling task: waits CONFIG_LD2410_LEARN_DELAY_SEC (time to leave the room), then
// records CONFIG_LD2410_LEARN_DURATION_SEC.
// With CONFIG_LD2410_LEARN_RADAR_AUTO, a radar with its own background noise detection (command 0x000B, newer
// LD2410B/C firmware) measures and stores its thresholds itself over the same times. Otherwise the ESP32 records
// the engineering reports (ld2410_capture_t, allocated meanwhile) and writes the thresholds it computes from them
// (quiet_learn_noise(), quiet_learn_compute_thresholds()); the radar keeps them in its flash.
// With on_noise (ESP32 presence engine): the engineering reports of the empty room are handed to it when the
// learning succeeded. Before the radar measures itself, the ESP32 records them for noise_seconds (after the whole
// delay), then turns the engineering mode off: the radar measures as with the stable firmware.
// Sends radar commands, so no other task may configure the radar meanwhile. The callbacks are optional.
// gate_cm: length of one gate, for the distances in the log (0: unknown).
quiet_learn_result_t quiet_learn_run(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                     uint16_t gate_cm, quiet_learn_noise_cb_t on_noise = nullptr,
                                     uint32_t noise_seconds = 0);

// Writes the LD2410 factory thresholds to every gate and reads them back; nothing is written when the radar holds
// them already. The farthest gates, no-one duration, resolution and Bluetooth stay as they are, the radar does not
// restart. Sends radar commands like quiet_learn_run(). True when the radar holds the factory thresholds.
bool quiet_learn_restore_factory(void);

// Energy per gate (0-100)
typedef struct {
    uint8_t moving[LD2410_GATE_COUNT];
    uint8_t still[LD2410_GATE_COUNT];
} quiet_learn_levels_t;

// Noise of each gate in a recording: the energy at rank ceil(frames * percentile / 100) once the frames of the gate
// are sorted by energy, i.e. the energy that percentile % of its frames stay at or below. 100 gives the highest
// energy recorded, a gate without frames 0.
void quiet_learn_noise(const ld2410_capture_t *capture, uint8_t percentile, quiet_learn_levels_t *out);

// New threshold per gate: noise + margin, at least the factory default (learning only makes the radar less
// sensitive, where the room is noisy), at most the factory default + max_raise (a gate where something moved
// during the recording does not go deaf) and at most 100 (gate ignored). The still thresholds of gates 0 and 1
// cannot be set on the LD2410 and are kept from current.
void quiet_learn_compute_thresholds(const quiet_learn_levels_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, uint8_t max_raise, ld2410_thresholds_t *out);
