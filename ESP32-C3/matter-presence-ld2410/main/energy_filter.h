/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32 energy filter (CONFIG_PRESENCE_ENERGY_FILTER, experimental): confirms the radar's detections from the energy
// of every gate (engineering mode), so that a single report slightly above a threshold does not make an empty room
// occupied. The radar reports a target as soon as one report is above a threshold, and then holds it for its no-one
// duration, so the filter counts the energies of each report, not the radar's target.
//
// A report is "above" when the radar reports a target and a gate in Range has an energy above the radar's own
// threshold for it (moving: gates 0 to the farthest moving gate; still: gates 2 to the farthest still gate, like the
// radar, whose still thresholds of gates 0 and 1 cannot be set). While the room is empty:
//   - a report with an energy at least `strong` above its threshold makes it occupied at once;
//   - weaker ones need `frames` reports above in a row (a report not above starts the count again).
// Once occupied, the radar decides alone (its target, held by its no-one duration), as without the filter. The
// filter only removes detections: it never reports someone the radar does not. Without energies (engineering mode
// off, e.g. just after a radar restart) the radar decides alone too.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <drivers/ld2410.h>

#include "presence.h"
#include "radar_settings.h"

typedef struct {
    uint8_t moving_threshold[LD2410_GATE_COUNT]; // the radar's thresholds (command 0x0061)
    uint8_t still_threshold[LD2410_GATE_COUNT];
    uint8_t max_moving_gate; // Range
    uint8_t max_still_gate;
    uint16_t gate_cm; // gate length, for max_cm
    uint16_t max_cm;  // CONFIG_PRESENCE_MAX_DISTANCE_CM: gates starting farther are not counted (0: all)
    uint8_t strong;   // energy this much above its threshold confirms at once
    uint8_t frames;   // reports above in a row that confirm a weaker detection
} energy_filter_config_t;

// The gate whose energy is the most above its threshold in a report
typedef struct {
    int16_t excess; // energy - threshold; 0 or less: no gate above its threshold
    uint8_t gate;
    bool moving; // moving energy (true) or still energy
    uint8_t energy;
    uint8_t threshold;
} energy_filter_peak_t;

typedef enum {
    ENERGY_FILTER_NO_EVENT,
    ENERGY_FILTER_STRONG,    // occupied at once: an energy at least `strong` above its threshold
    ENERGY_FILTER_CONFIRMED, // occupied after `frames` reports above in a row
    ENERGY_FILTER_IGNORED,   // reports above that stopped before `frames`
} energy_filter_event_t;

typedef struct {
    bool present;                // decision
    uint8_t weak_frames;         // reports above in a row while empty
    energy_filter_peak_t peak;   // highest of these reports (or the strong one)
    energy_filter_event_t event; // what the last report did
    uint8_t event_frames;        // reports above counted by the event
} energy_filter_state_t;

inline bool energy_filter_gate_counted(const energy_filter_config_t *cfg, uint8_t gate)
{
    return cfg->max_cm == 0 || (uint32_t)gate * cfg->gate_cm <= cfg->max_cm;
}

inline energy_filter_peak_t energy_filter_peak(const energy_filter_config_t *cfg, const ld2410_report_t *r)
{
    energy_filter_peak_t peak = {};
    peak.excess = INT16_MIN;
    for (uint8_t g = 0; g < r->moving_gates && g <= cfg->max_moving_gate && energy_filter_gate_counted(cfg, g); g++) {
        const int16_t excess = (int16_t)(r->moving_energy[g] - cfg->moving_threshold[g]);
        if (excess > peak.excess) {
            peak = { excess, g, true, r->moving_energy[g], cfg->moving_threshold[g] };
        }
    }
    for (uint8_t g = 2; g < r->still_gates && g <= cfg->max_still_gate && energy_filter_gate_counted(cfg, g); g++) {
        const int16_t excess = (int16_t)(r->still_energy[g] - cfg->still_threshold[g]);
        if (excess > peak.excess) {
            peak = { excess, g, false, r->still_energy[g], cfg->still_threshold[g] };
        }
    }
    return peak;
}

// One report: updates the state and returns the decision.
inline bool energy_filter_step(energy_filter_state_t *s, const energy_filter_config_t *cfg, const ld2410_report_t *r)
{
    s->event = ENERGY_FILTER_NO_EVENT;
    const bool radar = presence_from_target(&r->target, cfg->max_cm);
    if (s->present || !r->engineering) {
        // Occupied, or no energies: the radar decides alone
        s->present = radar;
        s->weak_frames = 0;
        return radar;
    }
    const energy_filter_peak_t peak = energy_filter_peak(cfg, r);
    if (!radar || peak.excess <= 0) {
        if (s->weak_frames > 0) {
            s->event = ENERGY_FILTER_IGNORED;
            s->event_frames = s->weak_frames;
        }
        s->weak_frames = 0;
        return false;
    }
    if (s->weak_frames == 0 || peak.excess > s->peak.excess) {
        s->peak = peak;
    }
    if (peak.excess >= cfg->strong) {
        s->peak = peak;
        s->present = true;
        s->event = ENERGY_FILTER_STRONG;
        s->event_frames = s->weak_frames + 1;
        s->weak_frames = 0;
        return true;
    }
    s->weak_frames++;
    if (s->weak_frames >= cfg->frames) {
        s->present = true;
        s->event = ENERGY_FILTER_CONFIRMED;
        s->event_frames = s->weak_frames;
        s->weak_frames = 0;
        return true;
    }
    return false;
}

// Runtime (energy_filter.cpp), fed by the LD2410 report hook.

// Sets the report hook. The filter stays inactive until energy_filter_configure().
void energy_filter_start(void);
// radar_cfg task: the thresholds, Range and gate length read from the radar. The decision starts from "empty" the
// first time.
void energy_filter_configure(const radar_settings_t *settings);
// Presence task: false while the filter is not configured (the radar decides alone), else the decision in *present.
bool energy_filter_presence(bool *present);
// Presence task: the last event not taken yet, for the log (out->event, out->event_frames, out->peak).
bool energy_filter_take_event(energy_filter_state_t *out);
// radar_cfg task: reports arrive (the last within 2 s) without energies, so the engineering mode must be turned on.
bool energy_filter_needs_engineering(int64_t now_ms);
