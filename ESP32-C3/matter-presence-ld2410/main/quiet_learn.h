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

// Runs one learning in the calling task: waits CONFIG_LD2410_LEARN_DELAY_SEC (time to leave the room), then
// records CONFIG_LD2410_LEARN_DURATION_SEC.
// With CONFIG_LD2410_LEARN_RADAR_AUTO, a radar with its own background noise detection (command 0x000B, newer
// LD2410B/C firmware) measures and stores its thresholds itself over the same times. Otherwise the ESP32 records
// the engineering reports (ld2410_capture_t, allocated meanwhile) and writes the thresholds it computes from them
// (quiet_learn_noise(), quiet_learn_compute_thresholds()); the radar keeps them in its flash.
// With CONFIG_LD2410_LEARN_PEAK, the ESP32 always records the room (at least 5 reports per second) and the
// thresholds come from the highest energy and the average of every gate (quiet_learn_room(),
// quiet_learn_peak_thresholds()); nothing is written when the radar holds them already.
// Sends radar commands, so no other task may configure the radar meanwhile. Both callbacks are optional.
// gate_cm: length of one gate, for the distances in the log (0: unknown).
quiet_learn_result_t quiet_learn_run(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                     uint16_t gate_cm);

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

// The empty room per gate, from a recording (CONFIG_LD2410_LEARN_PEAK)
typedef struct {
    uint32_t frames;
    quiet_learn_levels_t avg;   // average energy of the reports (rounded)
    quiet_learn_levels_t peak;  // highest energy of a report (what HLKRadarTool shows as the maximum)
    quiet_learn_levels_t noise; // 99th percentile, at least median + 3 robust spreads (1.4826 x MAD, at least 1)
    bool moving_valid[LD2410_GATE_COUNT]; // the gate has reports
    bool still_valid[LD2410_GATE_COUNT];
} quiet_learn_room_t;

void quiet_learn_room(const ld2410_capture_t *capture, quiet_learn_room_t *out);

typedef struct {
    uint8_t floor_percent; // at least this share of the factory default (rounded up)
    uint8_t min_margin;    // threshold = highest + the larger of min_margin and (highest - average)...
    uint8_t max_raise;     // ...at most the factory default + max_raise (and 99)
} quiet_learn_peak_params_t;

// What happened to a threshold learned from the highest energy (flags per gate)
#define QUIET_LEARN_FLOOR 0x01 // raised to the floor (a quiet gate)
#define QUIET_LEARN_LIMIT 0x02 // limited to the factory default + max_raise: something moved while recording
#define QUIET_LEARN_NOISE 0x04 // raised to the noise + 4
#define QUIET_LEARN_KEPT 0x08  // kept: no reports, or a still threshold of gates 0-1 (not settable)

// New threshold per gate: highest + the larger of min_margin and (highest - average), at least the noise + 4,
// between floor_percent % of the factory default and the factory default + max_raise (at most 99, never 100: the
// gate would be ignored). Floor and ceiling come from the factory defaults, never from current, so learning the same
// room again gives the same thresholds. Gates without reports and the still thresholds of gates 0 and 1 are kept
// from current. flags (may be nullptr): QUIET_LEARN_* of every gate.
void quiet_learn_peak_thresholds(const quiet_learn_room_t *room, const ld2410_thresholds_t *current,
                                 const quiet_learn_peak_params_t *params, ld2410_thresholds_t *out,
                                 quiet_learn_levels_t *flags);
