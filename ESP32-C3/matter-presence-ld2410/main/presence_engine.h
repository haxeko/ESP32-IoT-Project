/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32 presence engine (CONFIG_PRESENCE_ESP_ENGINE, experimental): the ESP32 decides presence from the energies
// the LD2410 reports in engineering mode (moving and still energy of each distance gate, 0-100, about 10 reports
// per second). The radar's own target and thresholds are not used. Plain C++ without ESP-IDF, so the host tests
// run it as it is; esp_presence.cpp feeds it on the ESP32.
//
// Channels: one per kind of energy (moving, still) and gate, each with its own learned thresholds:
//   - trigger: above the highest energy recorded in the empty room, with a margin;
//   - hold: above the usual noise (99th percentile, median + spread), always below trigger.
//
// Noise filter: each channel is the median of its last three energies before any comparison. A report that stands
// alone (an impulse, however strong) is dropped; two of three reports in a row must agree, which costs one report
// (100 ms) of delay. Moving and still energies are filtered separately.
//
// Presence (two thresholds, hysteresis):
//   - WAITING (empty): a gate counts as "strong" in a report when one of its filtered energies reaches its trigger.
//     Presence starts once `trigger_frames` of the last `trigger_window` reports were strong in the same place (a
//     gate or its neighbours: someone walking crosses gates, noise jumps around). With the filter, that is about
//     four reports (0.4 s) of a strong signal; isolated or two-report bursts never start it.
//   - PRESENT: a gate holds presence when one of its filtered energies reaches its hold threshold in `hold_frames`
//     of the last `hold_window` reports (breathing, small movements). Each report with that evidence restarts the
//     absence countdown; after `absence_ms` without it (the Occupancy Sensing HoldTime, 60 s by default) the room is
//     empty again and only a strong signal can start presence.
//   - A radar that sends nothing valid (or no energies) is a sensor fault, never an empty room: the countdown is
//     paused and starts again from the full time once data is back.
//
// Learning (LEARN_EMPTY): wait `learn_delay_ms` (time to leave), record `learn_duration_ms` of reports into a
// histogram per channel, then per channel: median (noise floor), robust spread (1.4826 x median absolute
// deviation), 99th percentile and maximum; thresholds from them (pe_compute_channel()). Channels too noisy to
// trigger, a radar that went silent and a room that was not empty enough are reported; the stored profile is only
// replaced when everything succeeded and was saved.
//
// Optional adaptation (RAM only, never stored): while the room has been empty and quiet for `adapt_quiet_ms`, a
// slow median tracker per channel may raise both thresholds by at most `adapt_max_up` when the noise floor has
// risen. Never below the learned values, never while present, counting down, learning or faulty.
//
// Time: milliseconds of a 32-bit counter that wraps around (every 49.7 days); only differences are used, and long
// spans are kept as flags, so the wrap does not matter.

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

#define PE_PROFILE_MAGIC 0x31504550u // "PEP1"
#define PE_PROFILE_VERSION 1

// Per-channel flags of a profile
#define PE_CH_NO_DATA 0x01   // no energies recorded (beyond the gates the radar reported while learning)
#define PE_CH_NOISY 0x02     // too noisy to trigger (trigger would be above 100); hold may still work
#define PE_CH_HOLD_HIGH 0x04 // hold threshold high: still people there may be missed
#define PE_CH_DEFAULT 0x08   // built-in default, not learned

typedef struct {
    uint8_t median;     // noise floor
    uint8_t spread_x10; // robust standard deviation x10: 1.4826 x median absolute deviation, at least sigma_min
    uint8_t p99;        // 99th percentile
    uint8_t max;        // highest energy recorded
    uint8_t hold;       // hold threshold, PE_OFF: cannot hold
    uint8_t trigger;    // trigger threshold, above hold; PE_OFF: cannot trigger
    uint8_t flags;      // PE_CH_*
    uint8_t reserved;
} pe_channel_t;

// Stored in NVS as one blob (esp_presence.cpp). Same layout on the ESP32 and the host (no padding).
typedef struct {
    uint32_t magic;      // PE_PROFILE_MAGIC
    uint16_t version;    // PE_PROFILE_VERSION
    uint16_t size;       // sizeof(pe_profile_t)
    uint16_t gate_cm;    // gate length while learning (0: any, built-in defaults)
    uint8_t learned;     // 1: from a learning, 0: built-in defaults
    uint8_t gates;       // gates with data while learning
    uint32_t frames;     // reports recorded
    uint32_t sequence;   // 1 for the first learning, then 2, 3...; 0: built-in defaults
    uint32_t duration_s; // recording time
    pe_channel_t ch[PE_KINDS][PE_GATES];
    uint32_t crc32; // CRC-32 of all the bytes before it
} pe_profile_t;

typedef struct {
    // Confirmation (reports, about 100 ms each)
    uint8_t trigger_frames; // strong reports needed...
    uint8_t trigger_window; // ...among this many last reports (at most PE_WINDOW_MAX)
    uint8_t hold_frames;    // reports reaching hold in a gate needed...
    uint8_t hold_window;    // ...among this many last reports (at most PE_WINDOW_MAX)
    uint32_t data_timeout_ms; // nothing valid (or no energies) for this long: sensor fault
    // Learning
    uint32_t learn_delay_ms;
    uint32_t learn_duration_ms;
    uint32_t learn_max_gap_ms;   // longest silence of the radar allowed while recording
    uint8_t learn_min_rate_x10;  // reports per second needed on average while recording, x10
    uint8_t max_noisy_percent;   // the learning fails when more of the channels in range than this cannot trigger
    // Thresholds from the noise statistics (energy units; _x10: tenths)
    uint8_t k_hold_x10;     // hold >= median + k_hold x spread
    uint8_t k_trigger_x10;  // trigger >= median + k_trigger x spread
    uint8_t hold_margin;    // hold >= 99th percentile + hold_margin
    uint8_t trigger_margin; // trigger >= maximum + trigger_margin
    uint8_t trigger_gap;    // trigger >= hold + trigger_gap
    uint8_t hold_floor;     // lowest hold threshold
    uint8_t trigger_floor;  // lowest trigger threshold
    uint8_t hold_high;      // PE_CH_HOLD_HIGH from this hold threshold
    uint8_t sigma_min_x10;  // lowest spread
    // Adaptation (RAM only)
    bool adapt;
    uint32_t adapt_quiet_ms;  // empty and quiet this long before adapting
    uint8_t adapt_max_up;     // highest raise of the thresholds
    uint16_t adapt_step_milli; // median tracker step per report, thousandths of an energy unit
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
    PE_SENSOR_STARTING,    // nothing received yet
    PE_SENSOR_OK,          // energies arrive
    PE_SENSOR_NO_DATA,     // nothing valid for data_timeout_ms
    PE_SENSOR_NO_ENERGY,   // reports without energies for data_timeout_ms (engineering mode off)
    PE_SENSOR_CONFIGURING, // a configuration session runs (the radar sends no reports meanwhile)
} pe_sensor_t;

typedef enum {
    PE_LEARN_IDLE,
    PE_LEARN_WAITING,   // time to leave the room
    PE_LEARN_RECORDING, // recording the empty room
} pe_learn_state_t;

typedef enum {
    PE_LEARN_NONE,          // no learning since boot
    PE_LEARN_OK,            // new profile saved and in use
    PE_LEARN_CANCELLED,     // cancelled, old profile kept
    PE_LEARN_NO_MEMORY,     // no memory for the histogram
    PE_LEARN_NO_DATA,       // too few reports with energies while recording
    PE_LEARN_DATA_GAP,      // the radar went silent while recording
    PE_LEARN_SETTINGS_CHANGED, // the gate length or Range changed while learning
    PE_LEARN_TOO_NOISY,     // too many channels too noisy to trigger (someone moved, a fan...)
    PE_LEARN_NO_USABLE,     // no channel in range can trigger
    PE_LEARN_SAVE_FAILED,   // could not be stored, old profile kept
} pe_learn_result_t;

// Learning outcome: why it failed and which channels are noisy
typedef struct {
    pe_learn_result_t result;
    uint32_t frames;         // reports with energies recorded
    uint32_t frames_no_energy;
    uint32_t max_gap_ms;     // longest silence while recording
    uint8_t channels;        // channels with data in range
    uint8_t noisy;           // ...that cannot trigger
    uint8_t hold_high;       // ...with a high hold threshold
} pe_learn_report_t;

// Saves a new profile (NVS on the ESP32); false keeps the old one.
typedef bool (*pe_save_cb_t)(const pe_profile_t *profile, void *ctx);

// Events since the last pe_take_events()
#define PE_EV_TRIGGERED 0x0001     // presence started (trigger_gate / trigger_kind / trigger_energy)
#define PE_EV_ABSENT 0x0002        // countdown over: empty again
#define PE_EV_SENSOR 0x0004        // sensor state changed
#define PE_EV_LEARN_PHASE 0x0008   // learning state changed (waiting, recording, idle)
#define PE_EV_LEARN_RESULT 0x0010  // learning finished (learn_report)
#define PE_EV_PROFILE 0x0020       // profile in use changed (learned, reset, gate length)
#define PE_EV_EVIDENCE 0x0040      // hold evidence appeared or stopped while present

typedef struct {
    pe_params_t p;
    pe_save_cb_t save;
    void *save_ctx;

    // Profiles: the stored one (learned or defaults) and the one in use (defaults while the gate length differs)
    pe_profile_t stored;
    pe_profile_t defaults;
    const pe_profile_t *active;
    pe_range_t range;
    uint32_t absence_ms;

    // Sensor
    pe_sensor_t sensor;
    bool any_frame;    // a report was received
    bool energy_frame; // a report with energies was received
    bool busy;         // configuration session running
    uint32_t start_ms;
    uint32_t last_any_ms;
    uint32_t last_energy_ms;
    uint32_t frames_ok;
    uint32_t frames_no_energy;
    uint32_t frames_bad;

    // Presence
    pe_presence_t presence;
    bool evidence;          // hold evidence in the last reports (present only)
    uint32_t last_evidence_ms;
    // Last reports, newest at window_pos: gates (bit = gate) reaching trigger and hold
    uint16_t trig_bits[PE_WINDOW_MAX];
    uint16_t hold_bits[PE_WINDOW_MAX];
    uint8_t window_pos;
    uint8_t window_count;
    uint8_t trigger_gate;   // last start of presence: gate, kind, energy, threshold
    uint8_t trigger_kind;
    uint8_t trigger_energy;
    uint8_t trigger_threshold;
    uint8_t evidence_gate;  // gate of the latest hold evidence

    // Latest energies and the highest of the last hold_window reports (diagnostics)
    pe_frame_t last;
    uint8_t recent[PE_WINDOW_MAX][PE_KINDS][PE_GATES];

    // Adaptation
    bool quiet;              // empty and quiet for adapt_quiet_ms
    uint32_t quiet_start_ms;
    int32_t level_milli[PE_KINDS][PE_GATES]; // tracked noise floor, thousandths
    uint8_t offset[PE_KINDS][PE_GATES];      // raise of both thresholds

    // Learning
    pe_learn_state_t learn_state;
    uint32_t learn_phase_ms;   // start of the current phase
    uint16_t (*hist)[PE_GATES][PE_MAX_ENERGY + 1]; // [kind][gate][energy] while recording
    uint32_t learn_frames;
    uint32_t learn_no_energy;
    uint32_t learn_last_ms;
    uint32_t learn_max_gap_ms;
    uint8_t learn_moving_gates;
    uint8_t learn_still_gates;
    uint16_t learn_gate_cm;
    pe_learn_report_t learn_report;
    // Profile computed by the last learning that got that far, also when it failed (which channels are noisy)
    pe_profile_t candidate;
    bool has_candidate;

    uint32_t events;
} pe_engine_t;

// Defaults of the parameters (the ESP32 takes them from menuconfig).
void pe_default_params(pe_params_t *p);

// Starts in WAITING with the built-in default profile (call pe_set_profile() with a stored one). The save callback
// stores a new profile after a successful learning.
void pe_init(pe_engine_t *e, const pe_params_t *p, pe_save_cb_t save, void *save_ctx, uint32_t now_ms);
void pe_deinit(pe_engine_t *e); // frees the learning histogram

// Built-in default profile: per gate, the LD2410 factory thresholds as hold and 15 more as trigger.
void pe_default_profile(pe_profile_t *out);

// A profile read from storage: false (and the defaults stay in use) when it is not a valid learned profile.
bool pe_set_profile(pe_engine_t *e, const pe_profile_t *profile, uint32_t now_ms);
// Back to the built-in defaults (the stored profile was erased).
void pe_reset_profile(pe_engine_t *e, uint32_t now_ms);

// Radar settings (gate length, Range) after each configuration session.
void pe_set_range(pe_engine_t *e, const pe_range_t *range, uint32_t now_ms);
// Occupancy Sensing HoldTime
void pe_set_absence_time(pe_engine_t *e, uint32_t absence_ms);
// A configuration session starts or ends (the radar does not report meanwhile).
void pe_set_busy(pe_engine_t *e, bool busy, uint32_t now_ms);
// A frame that failed its checks (driver count since the last call).
void pe_count_bad_frames(pe_engine_t *e, uint32_t count);

// One report. Never blocks; the work per report is a few hundred operations.
void pe_step(pe_engine_t *e, const pe_frame_t *frame, uint32_t now_ms);
// Time going by without reports (sensor timeouts, phases of the learning): call every 100 ms or so.
void pe_tick(pe_engine_t *e, uint32_t now_ms);

// LEARN_EMPTY: false when a learning runs already.
bool pe_learn_start(pe_engine_t *e, uint32_t now_ms);
// Stops a learning; false when none runs.
bool pe_learn_cancel(pe_engine_t *e, uint32_t now_ms);

// What the occupancy cluster gets: true while present with hold evidence, and while present with a sensor fault
// (no data is no proof of an empty room). False while counting down: the cluster counts HoldTime itself from that
// moment, like the engine.
bool pe_occupied_report(const pe_engine_t *e);
bool pe_sensor_fault(const pe_engine_t *e);
// Time left before the room is reported empty: UINT32_MAX when not counting down (no presence, hold evidence, or
// paused by a sensor fault).
uint32_t pe_remaining_ms(const pe_engine_t *e, uint32_t now_ms);
// Time left in the current learning phase (0 when idle)
uint32_t pe_learn_remaining_ms(const pe_engine_t *e, uint32_t now_ms);
// True when the thresholds in use come from a learning made with the current gate length.
bool pe_profile_learned(const pe_engine_t *e);
// Thresholds in use for a channel (with the adaptation)
uint8_t pe_hold_threshold(const pe_engine_t *e, int kind, int gate);
uint8_t pe_trigger_threshold(const pe_engine_t *e, int kind, int gate);
// Is the channel counted (Range, data)?
bool pe_channel_counted(const pe_engine_t *e, int kind, int gate);
// Highest energy of a channel in the last hold_window reports
uint8_t pe_recent_peak(const pe_engine_t *e, int kind, int gate);
// Filtered energy of a channel (median of its last three energies) as compared with the thresholds
uint8_t pe_filtered_energy(const pe_engine_t *e, int kind, int gate);

uint32_t pe_take_events(pe_engine_t *e);

// Thresholds of one channel from its recorded energies (histogram of energies 0-100, `frames` reports).
void pe_compute_channel(const pe_params_t *p, const uint16_t hist[PE_MAX_ENERGY + 1], uint32_t frames,
                        pe_channel_t *out);
// CRC-32 (IEEE 802.3) of a profile, without its crc32 field.
uint32_t pe_profile_crc(const pe_profile_t *profile);
// Checks magic, version, size and CRC.
bool pe_profile_valid(const pe_profile_t *profile);

const char *pe_presence_name(pe_presence_t presence);
const char *pe_sensor_name(pe_sensor_t sensor);
const char *pe_learn_state_name(pe_learn_state_t state);
const char *pe_learn_result_name(pe_learn_result_t result);
