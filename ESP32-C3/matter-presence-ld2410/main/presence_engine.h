/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32 presence engine (CONFIG_PRESENCE_ESP_ENGINE, experimental): the ESP32 decides presence from the energies
// the LD2410 reports in engineering mode (moving and still energy of each distance gate, 0-100, about 10 reports
// per second), with two thresholds per channel taken from the thresholds stored in the radar. The radar measures
// and stores those when the empty room is learned (or they are set with the HLKRadarTool app); the ESP32 reads them
// (command 0x0061) and does not store thresholds of its own. The radar's own target is not used. Plain C++ without
// ESP-IDF, so the host tests run it as it is; esp_presence.cpp feeds it on the ESP32.
//
// Channels: one per kind of energy (moving, still) and gate. From the radar threshold T of a channel:
//   - trigger: energy above T (+ trigger_margin), as the radar itself counts a target (T = 100: the radar ignores the
//     gate, so does the engine); and at least the noise of the empty room + trigger_noise_margin, so that a gate whose
//     noise reaches its radar threshold (radar thresholds left at the factory values, or lowered with HLKRadarTool)
//     does not start presence by itself (a noise above 100 - trigger_noise_margin: the channel cannot start it);
//   - hold: energy at or above T - drop, drop = hold_drop_percent % of T and at most hold_drop_max, so that someone
//     who keeps still (weaker energy than when moving in) stays detected; but never below the noise of the empty
//     room + hold_noise_margin, else the noise would hold presence for ever. Always below the trigger.
// The noise of every channel is recorded by the ESP32 while the empty room is learned (pe_noise_t, stored in NVS on
// the ESP32): the radar does not report it. Without it (never learned with this firmware, or learned with another
// gate length) the hold threshold is T and the trigger above T: no lower than the radar's own.
// Learning (pe_learn_thresholds()): the ESP32 records the empty room (the radar's own background noise detection
// is not used) and sets every radar threshold from the average and the highest filtered energy of its channel,
// written to the radar once: max + the larger of min_margin and (max - average), at least the noise +
// trigger_noise_margin - 1, between floor_percent % of the factory threshold and the factory threshold + max_raise.
// The still energy of gates 0 and 1 is not counted: the LD2410 has no still threshold there (it cannot be set).
//
// Noise filter: each channel is the median of its last three energies before any comparison. A report that stands
// alone (an impulse, however strong) is dropped; two of three reports in a row must agree, which costs one report
// (100 ms) of delay. Moving and still energies are filtered separately.
//
// Presence (two thresholds, hysteresis):
//   - WAITING (empty): a gate counts as "strong" in a report when one of its filtered energies reaches its trigger
//     (above the radar threshold and the noise). Presence starts once `trigger_frames` of the last `trigger_window`
//     reports were strong in the same place (a gate or its neighbours: someone walking crosses gates, noise jumps
//     around); by default 6 of 8, about 0.7 s, so that bursts of noise up to about 0.5 s are left out. Fast start:
//     a filtered moving energy at the trigger + `fast_margin` (20) in `fast_frames` (3) reports in a row, about
//     0.4 s, starts presence at once (someone walking in). Isolated or two-report bursts never start it.
//   - PRESENT: a gate holds presence when one of its filtered energies reaches its hold threshold (breathing, small
//     movements): at the person's gate and its `near_gates` neighbours in `near_frames` (1) of the last
//     `hold_window` reports, at any other gate in `hold_frames` (3) of them. The person's gate is where presence
//     started, then where a strong signal is confirmed again (as for a start: someone moving); noise that reaches a
//     hold threshold here and there does not move it. Each report with that evidence restarts the absence countdown;
//     after `absence_ms` without it (the Occupancy Sensing HoldTime, 60 s by default) the room is empty again and
//     only a strong signal can start presence.
//   - A radar that sends nothing valid (or no energies) is a sensor fault, never an empty room: the countdown is
//     paused and starts again from the full time once data is back.
//
// Time: milliseconds of a 32-bit counter that wraps around (every 49.7 days); only differences are used, so the
// wrap does not matter.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PE_GATES 9
#define PE_KINDS 2
#define PE_MOVING 0
#define PE_STILL 1
#define PE_MAX_ENERGY 100
// Threshold an energy (0-100) never reaches: the channel cannot trigger or hold
#define PE_OFF 101
// Reports kept for the confirmation windows
#define PE_WINDOW_MAX 16
// The LD2410 has still thresholds from this gate on
#define PE_FIRST_STILL_GATE 2

// Per-channel flags
#define PE_CH_NOT_SET 0x01       // no radar threshold (still energy of gates 0 and 1): not counted
#define PE_CH_IGNORED 0x02       // radar threshold 100: the radar ignores the gate, so does the engine
#define PE_CH_NOISE_HOLD 0x04    // the hold threshold was raised above the empty room noise (less drop than set)
#define PE_CH_NO_NOISE 0x08      // no noise recorded for it: hold = radar threshold
#define PE_CH_NOISE_TRIGGER 0x10 // the noise reaches the radar threshold: trigger raised above the noise

#define PE_NOISE_MAGIC 0x314E4550u // "PEN1"
#define PE_NOISE_VERSION 1
// Noise level of a channel without recording
#define PE_NOISE_UNKNOWN 0xFF

typedef struct {
    uint8_t radar;   // threshold stored in the radar (0-100)
    uint8_t trigger; // filtered energy at or above it is strong (radar + 1 + margin, at least noise + margin);
                     // PE_OFF: cannot trigger
    uint8_t hold;    // filtered energy at or above it keeps presence, below trigger; PE_OFF: cannot hold
    uint8_t flags;   // PE_CH_*
} pe_channel_t;

// The radar's thresholds (command 0x0061)
typedef struct {
    uint8_t moving[PE_GATES];
    uint8_t still[PE_GATES];
} pe_radar_thresholds_t;

// Noise of the empty room per channel, recorded while learning. Stored in NVS as one blob (esp_presence.cpp), same
// layout on the ESP32 and the host (no padding).
typedef struct {
    uint32_t magic;   // PE_NOISE_MAGIC
    uint16_t version; // PE_NOISE_VERSION
    uint16_t size;    // sizeof(pe_noise_t)
    uint16_t gate_cm; // gate length while recording
    uint16_t reserved;
    uint32_t frames;                   // reports recorded
    uint8_t level[PE_KINDS][PE_GATES]; // max(99th percentile, median + 3 spreads); PE_NOISE_UNKNOWN: no data
    uint8_t pad[2];
    uint32_t crc32; // CRC-32 of all the bytes before it
} pe_noise_t;

// The empty room recorded for a learning, per channel
typedef struct {
    uint32_t frames;
    uint8_t avg[PE_KINDS][PE_GATES];  // average energy of the reports (rounded)
    uint8_t max[PE_KINDS][PE_GATES];  // highest median of three successive reports (a report alone left out)
    uint8_t peak[PE_KINDS][PE_GATES]; // highest single report (what HLKRadarTool shows as the maximum)
    bool valid[PE_KINDS][PE_GATES];   // reports were recorded for the channel
} pe_room_t;

// Thresholds learned from the empty room
typedef struct {
    uint8_t floor_percent; // at least this share of the factory threshold (rounded up)
    uint8_t min_margin;    // threshold = max + the larger of min_margin and (max - average)...
    uint8_t max_raise;     // ...at most the factory threshold + max_raise (and 99)
} pe_learn_params_t;

// What happened to a learned threshold
#define PE_LEARN_FLOOR 0x01 // raised to the floor (a quiet channel)
#define PE_LEARN_LIMIT 0x02 // limited to factory + max_raise: the channel moved a lot while recording
#define PE_LEARN_KEPT 0x04  // kept as it was: no reports, or a still threshold of gates 0-1 (not settable)
#define PE_LEARN_NOISE 0x08 // raised to the noise + trigger_noise_margin - 1

// Thresholds in use
typedef struct {
    pe_channel_t ch[PE_KINDS][PE_GATES];
    bool from_radar; // false: the LD2410 factory thresholds until the radar's are read
    bool factory;    // the thresholds are the LD2410 factory ones (the empty room was never learned)
    bool noise;      // the hold thresholds follow a noise recording made with the current gate length
} pe_thresholds_t;

typedef struct {
    // Confirmation (reports, about 100 ms each)
    uint8_t trigger_frames; // strong reports needed...
    uint8_t trigger_window; // ...among this many last reports (at most PE_WINDOW_MAX)
    uint8_t fast_frames;    // or a moving energy at trigger + fast_margin in this many reports in a row (0: never)
    uint8_t fast_margin;
    uint8_t hold_frames;    // reports reaching hold in a gate needed...
    uint8_t hold_window;    // ...among this many last reports (at most PE_WINDOW_MAX)
    uint8_t near_frames;    // at the person's gate and near_gates on each side: reports reaching hold needed among
    uint8_t near_gates;     // the last hold_window (0: off, hold_frames there too)
    uint32_t data_timeout_ms; // nothing valid (or no energies) for this long: sensor fault
    // Trigger = radar threshold + 1 + trigger_margin, at least the recorded noise + trigger_noise_margin (at least 1)
    uint8_t trigger_margin;
    uint8_t trigger_noise_margin;
    // Hold threshold = radar threshold - min(hold_drop_max, radar threshold x hold_drop_percent / 100), at least
    // the recorded noise + hold_noise_margin (below trigger_noise_margin), below the trigger
    uint8_t hold_drop_percent;
    uint8_t hold_drop_max;
    uint8_t hold_noise_margin;
} pe_params_t;

// Gates counted: the radar's Range, limited by the farthest distance allowed.
typedef struct {
    uint16_t gate_cm;        // gate length (0: not known yet)
    uint8_t max_moving_gate; // farthest gate counted for the moving energy
    uint8_t max_still_gate;  // farthest gate counted for the still energy
} pe_range_t;

// One report of the radar
typedef struct {
    bool engineering;     // false: a report without energies (engineering mode off)
    uint8_t moving_gates; // energies valid for gates 0..moving_gates-1
    uint8_t still_gates;
    uint8_t moving[PE_GATES];
    uint8_t still[PE_GATES];
} pe_frame_t;

typedef enum {
    PE_PRESENCE_WAITING, // empty: waiting for a strong signal
    PE_PRESENCE_PRESENT, // present (counting down when no hold evidence)
} pe_presence_t;

typedef enum {
    PE_SENSOR_STARTING,  // nothing received yet
    PE_SENSOR_OK,        // energies arrive
    PE_SENSOR_NO_DATA,   // nothing valid for data_timeout_ms
    PE_SENSOR_NO_ENERGY, // reports without energies for data_timeout_ms (engineering mode off)
    PE_SENSOR_BUSY,      // the radar is being configured or learns the room (no reports or no energies meanwhile)
} pe_sensor_t;

// Events since the last pe_take_events()
#define PE_EV_TRIGGERED 0x0001  // presence started (trigger_gate / trigger_kind / trigger_energy)
#define PE_EV_ABSENT 0x0002     // countdown over: empty again
#define PE_EV_SENSOR 0x0004     // sensor state changed
#define PE_EV_THRESHOLDS 0x0008 // thresholds in use changed (read from the radar)
#define PE_EV_EVIDENCE 0x0010   // hold evidence appeared or stopped while present

typedef struct {
    pe_params_t p;
    pe_radar_thresholds_t radar; // last read from the radar (factory ones until then)
    pe_noise_t noise;            // recorded noise
    bool has_noise;
    pe_thresholds_t th;
    pe_range_t range;
    uint32_t absence_ms;

    // Sensor
    pe_sensor_t sensor;
    bool any_frame;    // a report was received
    bool energy_frame; // a report with energies was received
    bool busy;         // the radar is being configured or learns
    uint32_t start_ms;
    uint32_t last_any_ms;
    uint32_t last_energy_ms;
    uint32_t frames_ok;
    uint32_t frames_no_energy;
    uint32_t frames_bad;

    // Presence
    pe_presence_t presence;
    bool evidence; // hold evidence in the last reports (present only)
    uint32_t last_evidence_ms;
    // Last reports, newest at window_pos: gates (bit = gate) reaching trigger and hold
    uint16_t trig_bits[PE_WINDOW_MAX];
    uint16_t hold_bits[PE_WINDOW_MAX];
    uint16_t fast_bits[PE_WINDOW_MAX]; // gates with a moving energy at trigger + fast_margin
    uint8_t window_pos;
    uint8_t window_count;
    uint8_t trigger_gate; // last start of presence: gate, kind, energy, radar threshold, trigger of the channel
    uint8_t trigger_kind;
    uint8_t trigger_energy;
    uint8_t trigger_threshold;
    uint8_t trigger_strong;
    bool trigger_fast; // started by a very strong moving energy (trigger_strong: its fast level)
    uint8_t evidence_gate; // gate of the latest hold evidence
    bool evidence_near;    // found at the person's gates (near_frames)
    uint8_t person_gate;   // where presence started or a strong signal was confirmed again

    // Latest energies and the last reports of every channel (filter, diagnostics)
    pe_frame_t last;
    uint8_t recent[PE_WINDOW_MAX][PE_KINDS][PE_GATES];

    uint32_t events;
} pe_engine_t;

// Defaults of the parameters (the ESP32 takes them from menuconfig).
void pe_default_params(pe_params_t *p);

// Starts in WAITING with the LD2410 factory thresholds until pe_configure() gives the radar's.
void pe_init(pe_engine_t *e, const pe_params_t *p, uint32_t now_ms);

// LD2410 factory thresholds (HLK-LD2410 serial protocol; still gates 0 and 1 cannot be set: 0)
void pe_factory_thresholds(pe_radar_thresholds_t *out);
// Thresholds in use from the radar's (trigger above them, hold below them) and the noise recorded with the same
// gate length (nullptr: none, hold = radar threshold); trigger and hold stay above that noise.
void pe_compute_thresholds(const pe_params_t *p, const pe_radar_thresholds_t *radar, const pe_noise_t *noise,
                           pe_thresholds_t *out);

// Noise of every channel from histograms of the energies recorded in the empty room ([gate][energy 0-100], frames
// reports), with the gate length while recording.
void pe_noise_from_histograms(const uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1],
                              const uint16_t still[PE_GATES][PE_MAX_ENERGY + 1], uint32_t frames, uint16_t gate_cm,
                              pe_noise_t *out);
// CRC-32 (IEEE 802.3) of a noise record without its crc32 field; checks magic, version, size and CRC.
uint32_t pe_noise_crc(const pe_noise_t *noise);
bool pe_noise_valid(const pe_noise_t *noise);

// Radar settings after each configuration session: its thresholds and Range (gate length, farthest gates).
void pe_configure(pe_engine_t *e, const pe_radar_thresholds_t *radar, const pe_range_t *range, uint32_t now_ms);
// Noise recorded while learning (nullptr: none). False (nothing changed) when the record is not valid.
bool pe_set_noise(pe_engine_t *e, const pe_noise_t *noise, uint32_t now_ms);
// The empty room from a recording: histograms of the energies ([gate][energy 0-100], frames reports) and the highest
// filtered energy of every gate.
void pe_room_from_histograms(const uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1],
                             const uint16_t still[PE_GATES][PE_MAX_ENERGY + 1], const uint8_t moving_max[PE_GATES],
                             const uint8_t still_max[PE_GATES], uint32_t frames, pe_room_t *out);
// Radar thresholds learned from the empty room (see the top of this file); noise (nullptr: none) from the same
// recording, current: what the radar holds (kept where nothing was recorded, and for the still gates 0 and 1).
// flags (may be nullptr): PE_LEARN_* of every channel.
void pe_learn_thresholds(const pe_params_t *p, const pe_learn_params_t *lp, const pe_room_t *room,
                         const pe_noise_t *noise, const pe_radar_thresholds_t *current, pe_radar_thresholds_t *out,
                         uint8_t flags[PE_KINDS][PE_GATES]);
// Occupancy Sensing HoldTime
void pe_set_absence_time(pe_engine_t *e, uint32_t absence_ms);
// A configuration session or a learning of the radar starts or ends (no reports or no energies meanwhile).
void pe_set_busy(pe_engine_t *e, bool busy, uint32_t now_ms);
// A frame that failed its checks (driver count since the last call).
void pe_count_bad_frames(pe_engine_t *e, uint32_t count);

// One report. Never blocks; the work per report is a few hundred operations.
void pe_step(pe_engine_t *e, const pe_frame_t *frame, uint32_t now_ms);
// Time going by without reports (sensor timeouts, countdown): call every 100 ms or so.
void pe_tick(pe_engine_t *e, uint32_t now_ms);

// What the occupancy cluster gets: true while present with hold evidence, and while present with a sensor fault
// (no data is no proof of an empty room). False while counting down: the cluster counts HoldTime itself from that
// moment, like the engine.
bool pe_occupied_report(const pe_engine_t *e);
bool pe_sensor_fault(const pe_engine_t *e);
// Time left before the room is reported empty: UINT32_MAX when not counting down (no presence, hold evidence, or
// paused by a sensor fault).
uint32_t pe_remaining_ms(const pe_engine_t *e, uint32_t now_ms);
// Thresholds in use for a channel
uint8_t pe_hold_threshold(const pe_engine_t *e, int kind, int gate);
uint8_t pe_trigger_threshold(const pe_engine_t *e, int kind, int gate);
// Is the channel counted (Range, radar threshold set and not 100)?
bool pe_channel_counted(const pe_engine_t *e, int kind, int gate);
// Highest energy of a channel in the last hold_window reports
uint8_t pe_recent_peak(const pe_engine_t *e, int kind, int gate);
// Filtered energy of a channel (median of its last three energies) as compared with the thresholds
uint8_t pe_filtered_energy(const pe_engine_t *e, int kind, int gate);

uint32_t pe_take_events(pe_engine_t *e);

const char *pe_presence_name(pe_presence_t presence);
const char *pe_sensor_name(pe_sensor_t sensor);
