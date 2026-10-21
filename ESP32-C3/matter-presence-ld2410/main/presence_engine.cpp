/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>

// Built only with CONFIG_PRESENCE_ESP_ENGINE (the host tests define it)
#if CONFIG_PRESENCE_ESP_ENGINE

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "presence_engine.h"

static_assert(sizeof(pe_channel_t) == 8, "pe_channel_t is stored in NVS");
static_assert(sizeof(pe_profile_t) == 172, "pe_profile_t is stored in NVS: keep its layout");
static_assert(PE_GATES <= 16, "gates are bits of a uint16_t");

// LD2410 factory thresholds, the built-in profile until a learning. The radar cannot set the still thresholds of
// gates 0 and 1; the ESP32 can, and uses the one of gate 2.
static const uint8_t kFactoryMoving[PE_GATES] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kFactoryStill[PE_GATES] = { 40, 40, 40, 40, 30, 30, 20, 20, 20 };
static constexpr uint8_t kDefaultTriggerRaise = 15;

// ---------------------------------------------------------------------------------------------------------------------
// Profiles

uint32_t pe_profile_crc(const pe_profile_t *profile)
{
    const uint8_t *bytes = (const uint8_t *)profile;
    const size_t len = offsetof(pe_profile_t, crc32);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool pe_profile_valid(const pe_profile_t *profile)
{
    if (profile->magic != PE_PROFILE_MAGIC || profile->version != PE_PROFILE_VERSION ||
        profile->size != sizeof(pe_profile_t) || profile->crc32 != pe_profile_crc(profile) || profile->learned > 1 ||
        profile->gates > PE_GATES) {
        return false;
    }
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            const pe_channel_t &ch = profile->ch[kind][g];
            if (ch.hold > PE_OFF || ch.trigger > PE_OFF || (ch.hold == PE_OFF && ch.trigger != PE_OFF) ||
                (ch.trigger != PE_OFF && ch.trigger <= ch.hold)) {
                return false;
            }
        }
    }
    return true;
}

void pe_default_profile(pe_profile_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = PE_PROFILE_MAGIC;
    out->version = PE_PROFILE_VERSION;
    out->size = sizeof(pe_profile_t);
    out->gates = PE_GATES;
    for (int g = 0; g < PE_GATES; g++) {
        const uint8_t factory[PE_KINDS] = { kFactoryMoving[g], kFactoryStill[g] };
        for (int kind = 0; kind < PE_KINDS; kind++) {
            pe_channel_t &ch = out->ch[kind][g];
            ch.hold = factory[kind];
            ch.trigger = (uint8_t)std::min(PE_MAX_ENERGY, factory[kind] + kDefaultTriggerRaise);
            ch.flags = PE_CH_DEFAULT;
        }
    }
    out->crc32 = pe_profile_crc(out);
}

// Energy at rank ceil(n x percent / 100) of the n values counted in hist (the value that `percent` % of them stay
// at or below).
static uint8_t quantile(const uint32_t hist[PE_MAX_ENERGY + 1], uint32_t n, uint32_t percent)
{
    uint64_t rank = ((uint64_t)n * percent + 99) / 100;
    if (rank == 0) {
        rank = 1;
    }
    uint64_t seen = 0;
    for (int v = 0; v <= PE_MAX_ENERGY; v++) {
        seen += hist[v];
        if (seen >= rank) {
            return (uint8_t)v;
        }
    }
    return PE_MAX_ENERGY;
}

static uint32_t ceil_div(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

void pe_compute_channel(const pe_params_t *p, const uint16_t hist[PE_MAX_ENERGY + 1], uint32_t frames,
                        pe_channel_t *out)
{
    (void)frames;
    memset(out, 0, sizeof(*out));
    uint32_t counts[PE_MAX_ENERGY + 1];
    uint32_t n = 0;
    uint8_t highest = 0;
    for (int v = 0; v <= PE_MAX_ENERGY; v++) {
        counts[v] = hist[v];
        n += hist[v];
        if (hist[v] != 0) {
            highest = (uint8_t)v;
        }
    }
    if (n == 0) {
        out->hold = PE_OFF;
        out->trigger = PE_OFF;
        out->flags = PE_CH_NO_DATA;
        return;
    }
    const uint8_t median = quantile(counts, n, 50);
    // Median absolute deviation: the median of |energy - median|, robust against the rare bursts that a standard
    // deviation would follow.
    uint32_t deviations[PE_MAX_ENERGY + 1] = {};
    for (int v = 0; v <= PE_MAX_ENERGY; v++) {
        deviations[v > median ? v - median : median - v] += counts[v];
    }
    const uint8_t mad = quantile(deviations, n, 50);
    // 1.4826 x MAD estimates the standard deviation of normally distributed noise
    uint32_t spread_x10 = ((uint32_t)mad * 14826 + 500) / 1000;
    spread_x10 = std::max<uint32_t>(spread_x10, p->sigma_min_x10);
    spread_x10 = std::min<uint32_t>(spread_x10, 255);

    out->median = median;
    out->spread_x10 = (uint8_t)spread_x10;
    out->p99 = quantile(counts, n, 99);
    out->max = highest;

    // Hold: above the usual noise of the channel, so that the empty room rarely reaches it in several reports.
    uint32_t hold = ceil_div((uint32_t)median * 100 + (uint32_t)p->k_hold_x10 * spread_x10, 100);
    hold = std::max<uint32_t>(hold, (uint32_t)out->p99 + p->hold_margin);
    hold = std::max<uint32_t>(hold, p->hold_floor);
    // Trigger: above everything the empty room did while recording, and clearly above hold.
    uint32_t trigger = ceil_div((uint32_t)median * 100 + (uint32_t)p->k_trigger_x10 * spread_x10, 100);
    trigger = std::max<uint32_t>(trigger, (uint32_t)highest + p->trigger_margin);
    trigger = std::max<uint32_t>(trigger, hold + std::max<uint32_t>(p->trigger_gap, 1));
    trigger = std::max<uint32_t>(trigger, p->trigger_floor);

    if (hold >= PE_MAX_ENERGY) {
        // Even holding would need an energy the radar cannot report: the channel is not used.
        out->hold = PE_OFF;
        out->trigger = PE_OFF;
        out->flags |= PE_CH_NOISY;
        return;
    }
    out->hold = (uint8_t)hold;
    if (trigger > PE_MAX_ENERGY) {
        out->trigger = PE_OFF;
        out->flags |= PE_CH_NOISY;
    } else {
        out->trigger = (uint8_t)trigger;
    }
    if (hold >= p->hold_high) {
        out->flags |= PE_CH_HOLD_HIGH;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine

void pe_default_params(pe_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->trigger_frames = 3;
    p->trigger_window = 4;
    p->hold_frames = 3;
    p->hold_window = 10;
    p->data_timeout_ms = 2000;
    p->learn_delay_ms = 20 * 1000;
    p->learn_duration_ms = 60 * 1000;
    p->learn_max_gap_ms = 1500;
    p->learn_min_rate_x10 = 80;
    p->max_noisy_percent = 50;
    p->k_hold_x10 = 30;
    p->k_trigger_x10 = 60;
    p->hold_margin = 2;
    p->trigger_margin = 5;
    p->trigger_gap = 6;
    p->hold_floor = 5;
    p->trigger_floor = 12;
    p->hold_high = 60;
    p->sigma_min_x10 = 10;
    p->adapt = true;
    p->adapt_quiet_ms = 5 * 60 * 1000;
    p->adapt_max_up = 6;
    p->adapt_step_milli = 2;
}

static void sanitize_params(pe_params_t *p)
{
    p->trigger_window = std::clamp<uint8_t>(p->trigger_window, 1, PE_WINDOW_MAX);
    p->trigger_frames = std::clamp<uint8_t>(p->trigger_frames, 1, p->trigger_window);
    p->hold_window = std::clamp<uint8_t>(p->hold_window, 1, PE_WINDOW_MAX);
    p->hold_frames = std::clamp<uint8_t>(p->hold_frames, 1, p->hold_window);
    p->data_timeout_ms = std::max<uint32_t>(p->data_timeout_ms, 300);
    p->learn_duration_ms = std::max<uint32_t>(p->learn_duration_ms, 1000);
}

static void clear_window(pe_engine_t *e)
{
    e->window_count = 0;
    e->window_pos = 0;
    memset(e->trig_bits, 0, sizeof(e->trig_bits));
    memset(e->hold_bits, 0, sizeof(e->hold_bits));
    memset(e->recent, 0, sizeof(e->recent));
}

static void reset_adaptation(pe_engine_t *e, uint32_t now_ms)
{
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            e->level_milli[kind][g] = (int32_t)e->active->ch[kind][g].median * 1000;
            e->offset[kind][g] = 0;
        }
    }
    e->quiet = false;
    e->quiet_start_ms = now_ms;
}

// The stored profile when it was learned with the current gate length (or the gate length is not known yet),
// else the built-in defaults: thresholds learned for 0.75 m gates mean nothing for 0.2 m gates.
static void select_active(pe_engine_t *e, uint32_t now_ms)
{
    const bool usable = e->stored.learned && (e->range.gate_cm == 0 || e->stored.gate_cm == e->range.gate_cm);
    const pe_profile_t *active = usable ? &e->stored : &e->defaults;
    if (active != e->active) {
        e->active = active;
        e->events |= PE_EV_PROFILE;
    }
    // New thresholds: the reports counted so far were judged with the old ones.
    clear_window(e);
    reset_adaptation(e, now_ms);
}

void pe_init(pe_engine_t *e, const pe_params_t *p, pe_save_cb_t save, void *save_ctx, uint32_t now_ms)
{
    memset(e, 0, sizeof(*e));
    e->p = *p;
    sanitize_params(&e->p);
    e->save = save;
    e->save_ctx = save_ctx;
    pe_default_profile(&e->defaults);
    e->stored = e->defaults;
    e->active = &e->defaults;
    e->range = { 0, PE_GATES - 1, PE_GATES - 1 };
    e->absence_ms = 60 * 1000;
    e->sensor = PE_SENSOR_STARTING;
    e->start_ms = now_ms;
    e->presence = PE_PRESENCE_WAITING;
    reset_adaptation(e, now_ms);
}

void pe_deinit(pe_engine_t *e)
{
    free(e->hist);
    e->hist = nullptr;
}

bool pe_set_profile(pe_engine_t *e, const pe_profile_t *profile, uint32_t now_ms)
{
    if (!pe_profile_valid(profile) || !profile->learned) {
        return false;
    }
    e->stored = *profile;
    e->events |= PE_EV_PROFILE;
    select_active(e, now_ms);
    return true;
}

void pe_reset_profile(pe_engine_t *e, uint32_t now_ms)
{
    e->stored = e->defaults;
    e->events |= PE_EV_PROFILE;
    select_active(e, now_ms);
}

static void learn_finish(pe_engine_t *e, pe_learn_result_t result)
{
    free(e->hist);
    e->hist = nullptr;
    e->learn_state = PE_LEARN_IDLE;
    e->learn_report.result = result;
    e->learn_report.frames = e->learn_frames;
    e->learn_report.frames_no_energy = e->learn_no_energy;
    e->learn_report.max_gap_ms = e->learn_max_gap_ms;
    e->events |= PE_EV_LEARN_RESULT | PE_EV_LEARN_PHASE;
}

void pe_set_range(pe_engine_t *e, const pe_range_t *range, uint32_t now_ms)
{
    pe_range_t r = *range;
    r.max_moving_gate = std::min<uint8_t>(r.max_moving_gate, PE_GATES - 1);
    r.max_still_gate = std::min<uint8_t>(r.max_still_gate, PE_GATES - 1);
    const bool gate_changed = e->range.gate_cm != 0 && r.gate_cm != 0 && r.gate_cm != e->range.gate_cm;
    const bool changed = r.gate_cm != e->range.gate_cm || r.max_moving_gate != e->range.max_moving_gate ||
                         r.max_still_gate != e->range.max_still_gate;
    e->range = r;
    if (gate_changed && e->learn_state != PE_LEARN_IDLE) {
        learn_finish(e, PE_LEARN_SETTINGS_CHANGED);
    }
    if (changed) {
        select_active(e, now_ms);
    }
}

void pe_set_absence_time(pe_engine_t *e, uint32_t absence_ms)
{
    e->absence_ms = std::max<uint32_t>(absence_ms, 1000);
}

void pe_count_bad_frames(pe_engine_t *e, uint32_t count)
{
    e->frames_bad += count;
}

bool pe_sensor_fault(const pe_engine_t *e)
{
    return e->sensor != PE_SENSOR_OK;
}

static pe_sensor_t evaluate_sensor(const pe_engine_t *e, uint32_t now_ms)
{
    const uint32_t timeout = e->p.data_timeout_ms;
    pe_sensor_t silent = e->busy ? PE_SENSOR_CONFIGURING : PE_SENSOR_NO_DATA;
    if (!e->any_frame) {
        return now_ms - e->start_ms >= timeout ? silent : PE_SENSOR_STARTING;
    }
    if (now_ms - e->last_any_ms >= timeout) {
        return silent;
    }
    if (!e->energy_frame || now_ms - e->last_energy_ms >= timeout) {
        // Reports keep coming, but without energies: the engineering mode is off (radar restarted)
        if (!e->energy_frame && now_ms - e->start_ms < timeout) {
            return PE_SENSOR_STARTING;
        }
        return e->busy ? PE_SENSOR_CONFIGURING : PE_SENSOR_NO_ENERGY;
    }
    return PE_SENSOR_OK;
}

static void update_sensor(pe_engine_t *e, uint32_t now_ms)
{
    const pe_sensor_t sensor = evaluate_sensor(e, now_ms);
    if (sensor == e->sensor) {
        return;
    }
    const bool was_ok = e->sensor == PE_SENSOR_OK;
    e->sensor = sensor;
    e->events |= PE_EV_SENSOR;
    if (was_ok) {
        // Fault: the reports in the windows no longer follow each other
        clear_window(e);
        e->quiet = false;
    } else if (sensor == PE_SENSOR_OK) {
        // Data back. Nothing is known about the time without data: a present room starts its full countdown again
        // (it is never made empty because the radar was silent).
        if (e->presence == PE_PRESENCE_PRESENT) {
            e->last_evidence_ms = now_ms;
        }
        clear_window(e);
        e->quiet = false;
        e->quiet_start_ms = now_ms;
    }
}

void pe_set_busy(pe_engine_t *e, bool busy, uint32_t now_ms)
{
    e->busy = busy;
    update_sensor(e, now_ms);
}

uint8_t pe_hold_threshold(const pe_engine_t *e, int kind, int gate)
{
    const uint8_t hold = e->active->ch[kind][gate].hold;
    if (hold >= PE_OFF) {
        return PE_OFF;
    }
    return (uint8_t)std::min<int>(hold + e->offset[kind][gate], PE_MAX_ENERGY - 1);
}

uint8_t pe_trigger_threshold(const pe_engine_t *e, int kind, int gate)
{
    const uint8_t trigger = e->active->ch[kind][gate].trigger;
    if (trigger >= PE_OFF) {
        return PE_OFF;
    }
    return (uint8_t)std::min<int>(trigger + e->offset[kind][gate], PE_MAX_ENERGY);
}

static uint8_t max_gate(const pe_engine_t *e, int kind)
{
    return kind == PE_MOVING ? e->range.max_moving_gate : e->range.max_still_gate;
}

bool pe_channel_counted(const pe_engine_t *e, int kind, int gate)
{
    return gate <= max_gate(e, kind) && !(e->active->ch[kind][gate].flags & PE_CH_NO_DATA);
}

// Reports among the last `window` with one of the gates in mask set
static int count_reports(const pe_engine_t *e, const uint16_t *bits, int window, uint16_t mask)
{
    const int n = std::min<int>(window, e->window_count);
    int count = 0;
    for (int i = 0; i < n; i++) {
        const int idx = (e->window_pos + PE_WINDOW_MAX - i) % PE_WINDOW_MAX;
        if (bits[idx] & mask) {
            count++;
        }
    }
    return count;
}

// Gate where presence is confirmed (trigger_frames strong reports among the last trigger_window, at that gate or
// a neighbour), -1 if none.
static int confirmed_gate(const pe_engine_t *e)
{
    for (int g = 0; g < PE_GATES; g++) {
        uint16_t mask = (uint16_t)(1u << g);
        if (g > 0) {
            mask |= (uint16_t)(1u << (g - 1));
        }
        if (g < PE_GATES - 1) {
            mask |= (uint16_t)(1u << (g + 1));
        }
        if (count_reports(e, e->trig_bits, e->p.trigger_window, mask) >= e->p.trigger_frames) {
            return g;
        }
    }
    return -1;
}

// Gate with hold evidence (hold_frames reports reaching hold among the last hold_window), -1 if none. Counted per
// gate: noise reaches hold here and there, a person keeps doing it at the same place.
static int evidence_gate(const pe_engine_t *e)
{
    for (int g = 0; g < PE_GATES; g++) {
        if (count_reports(e, e->hold_bits, e->p.hold_window, (uint16_t)(1u << g)) >= e->p.hold_frames) {
            return g;
        }
    }
    return -1;
}

uint8_t pe_recent_peak(const pe_engine_t *e, int kind, int gate)
{
    const int n = std::min<int>(e->p.hold_window, e->window_count);
    uint8_t peak = 0;
    for (int i = 0; i < n; i++) {
        const int idx = (e->window_pos + PE_WINDOW_MAX - i) % PE_WINDOW_MAX;
        peak = std::max(peak, e->recent[idx][kind][gate]);
    }
    return peak;
}

static void go_absent(pe_engine_t *e, uint32_t now_ms)
{
    e->presence = PE_PRESENCE_WAITING;
    e->evidence = false;
    clear_window(e);
    e->quiet = false;
    e->quiet_start_ms = now_ms;
    e->events |= PE_EV_ABSENT;
}

static void check_countdown(pe_engine_t *e, uint32_t now_ms)
{
    if (e->presence == PE_PRESENCE_PRESENT && !e->evidence && !pe_sensor_fault(e) &&
        now_ms - e->last_evidence_ms >= e->absence_ms) {
        go_absent(e, now_ms);
    }
}

// Median of the last three energies of a channel (impulse noise filter): a report that stands alone, however
// strong, is dropped; two of three reports must agree. Right after a clear (fault, new thresholds) the newest two
// must both be high, and a lone report counts as nothing.
static uint8_t filtered(const pe_engine_t *e, int kind, int gate)
{
    const uint8_t a = e->recent[e->window_pos][kind][gate];
    if (e->window_count < 2) {
        return 0;
    }
    const uint8_t b = e->recent[(e->window_pos + PE_WINDOW_MAX - 1) % PE_WINDOW_MAX][kind][gate];
    if (e->window_count < 3) {
        return std::min(a, b);
    }
    const uint8_t c = e->recent[(e->window_pos + PE_WINDOW_MAX - 2) % PE_WINDOW_MAX][kind][gate];
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

uint8_t pe_filtered_energy(const pe_engine_t *e, int kind, int gate)
{
    return filtered(e, kind, gate);
}

static void evaluate(pe_engine_t *e, const pe_frame_t *f, uint32_t now_ms, uint16_t *trig_out)
{
    uint16_t trig = 0;
    uint16_t hold = 0;
    e->window_pos = (uint8_t)((e->window_pos + 1) % PE_WINDOW_MAX);
    e->window_count = (uint8_t)std::min<int>(e->window_count + 1, PE_WINDOW_MAX);
    uint8_t(*recent)[PE_GATES] = e->recent[e->window_pos];
    memset(recent, 0, sizeof(e->recent[0]));
    for (int kind = 0; kind < PE_KINDS; kind++) {
        const uint8_t gates = kind == PE_MOVING ? f->moving_gates : f->still_gates;
        const uint8_t *energy = kind == PE_MOVING ? f->moving : f->still;
        for (int g = 0; g < gates && g < PE_GATES; g++) {
            recent[kind][g] = energy[g];
        }
    }
    // Strongest channel above its trigger in this report (log of the start of presence)
    int best_excess = -1;
    uint8_t best_gate = 0;
    uint8_t best_kind = 0;
    uint8_t best_energy = 0;
    uint8_t best_threshold = 0;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            if (!pe_channel_counted(e, kind, g)) {
                continue;
            }
            const uint8_t x = filtered(e, kind, g);
            const uint8_t t = pe_trigger_threshold(e, kind, g);
            if (x >= t) {
                trig |= (uint16_t)(1u << g);
                if (x - t > best_excess) {
                    best_excess = x - t;
                    best_gate = (uint8_t)g;
                    best_kind = (uint8_t)kind;
                    best_energy = x;
                    best_threshold = t;
                }
            }
            if (x >= pe_hold_threshold(e, kind, g)) {
                hold |= (uint16_t)(1u << g);
            }
        }
    }
    e->trig_bits[e->window_pos] = trig;
    e->hold_bits[e->window_pos] = hold;
    *trig_out = trig;

    if (e->presence == PE_PRESENCE_WAITING) {
        const int g = confirmed_gate(e);
        if (g >= 0) {
            e->presence = PE_PRESENCE_PRESENT;
            e->evidence = true;
            e->evidence_gate = (uint8_t)g;
            e->last_evidence_ms = now_ms;
            e->trigger_gate = best_excess >= 0 ? best_gate : (uint8_t)g;
            e->trigger_kind = best_kind;
            e->trigger_energy = best_energy;
            e->trigger_threshold = best_threshold;
            e->quiet = false;
            e->events |= PE_EV_TRIGGERED | PE_EV_EVIDENCE;
        }
        return;
    }

    const int g = evidence_gate(e);
    const bool evidence = g >= 0;
    if (evidence) {
        e->last_evidence_ms = now_ms;
        e->evidence_gate = (uint8_t)g;
    }
    if (evidence != e->evidence) {
        e->evidence = evidence;
        e->events |= PE_EV_EVIDENCE;
    }
    check_countdown(e, now_ms);
}

// Slow median tracker per channel while the room is empty and quiet; never stored, never lowers a threshold below
// its learned value.
static void adapt(pe_engine_t *e, const pe_frame_t *f, uint32_t now_ms, uint16_t trig)
{
    if (!e->p.adapt) {
        return;
    }
    const bool allowed = e->presence == PE_PRESENCE_WAITING && e->sensor == PE_SENSOR_OK &&
                         e->learn_state == PE_LEARN_IDLE && e->active->learned;
    if (!allowed || trig != 0) {
        // Present, counting down, learning, or something strong passed: start the quiet time again
        e->quiet = false;
        e->quiet_start_ms = now_ms;
        return;
    }
    if (!e->quiet) {
        if (now_ms - e->quiet_start_ms < e->p.adapt_quiet_ms) {
            return;
        }
        e->quiet = true; // kept as a flag: the quiet time may last longer than the 32-bit clock can measure
    }
    for (int kind = 0; kind < PE_KINDS; kind++) {
        const uint8_t gates = kind == PE_MOVING ? f->moving_gates : f->still_gates;
        const uint8_t *energy = kind == PE_MOVING ? f->moving : f->still;
        for (int g = 0; g < gates && g < PE_GATES; g++) {
            const pe_channel_t &ch = e->active->ch[kind][g];
            if (!pe_channel_counted(e, kind, g) || ch.hold >= PE_OFF) {
                continue;
            }
            int32_t &level = e->level_milli[kind][g];
            const int32_t target = (int32_t)energy[g] * 1000;
            const int32_t step = e->p.adapt_step_milli;
            if (target > level) {
                level = std::min(level + step, target);
            } else if (target < level) {
                level = std::max(level - step, target);
            }
            const int32_t floor = (int32_t)ch.median * 1000;
            const int32_t ceiling = floor + (int32_t)e->p.adapt_max_up * 1000;
            level = std::clamp(level, floor, ceiling);
            int offset = (level - floor) / 1000;
            // The thresholds stay reachable
            offset = std::min(offset, PE_MAX_ENERGY - 1 - ch.hold);
            if (ch.trigger < PE_OFF) {
                offset = std::min(offset, PE_MAX_ENERGY - ch.trigger);
            }
            e->offset[kind][g] = (uint8_t)std::max(offset, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Learning

bool pe_learn_start(pe_engine_t *e, uint32_t now_ms)
{
    if (e->learn_state != PE_LEARN_IDLE) {
        return false;
    }
    e->learn_state = PE_LEARN_WAITING;
    e->learn_phase_ms = now_ms;
    e->learn_frames = 0;
    e->learn_no_energy = 0;
    e->learn_max_gap_ms = 0;
    memset(&e->learn_report, 0, sizeof(e->learn_report));
    e->events |= PE_EV_LEARN_PHASE;
    return true;
}

bool pe_learn_cancel(pe_engine_t *e, uint32_t now_ms)
{
    (void)now_ms;
    if (e->learn_state == PE_LEARN_IDLE) {
        return false;
    }
    learn_finish(e, PE_LEARN_CANCELLED);
    return true;
}

static void learn_record(pe_engine_t *e, const pe_frame_t *f, uint32_t now_ms)
{
    e->learn_max_gap_ms = std::max(e->learn_max_gap_ms, now_ms - e->learn_last_ms);
    e->learn_last_ms = now_ms;
    e->learn_frames++;
    e->learn_moving_gates = std::max(e->learn_moving_gates, std::min<uint8_t>(f->moving_gates, PE_GATES));
    e->learn_still_gates = std::max(e->learn_still_gates, std::min<uint8_t>(f->still_gates, PE_GATES));
    for (int kind = 0; kind < PE_KINDS; kind++) {
        const uint8_t gates = kind == PE_MOVING ? f->moving_gates : f->still_gates;
        const uint8_t *energy = kind == PE_MOVING ? f->moving : f->still;
        for (int g = 0; g < gates && g < PE_GATES; g++) {
            uint16_t &count = e->hist[kind][g][std::min<uint8_t>(energy[g], PE_MAX_ENERGY)];
            if (count < UINT16_MAX) {
                count++;
            }
        }
    }
}

static void learn_analyze(pe_engine_t *e)
{
    const uint32_t duration_s = e->p.learn_duration_ms / 1000;
    const uint32_t min_frames = (uint32_t)((uint64_t)e->p.learn_duration_ms * e->p.learn_min_rate_x10 / 10000);
    // More than 5 % of the reports without energies: the engineering mode was off part of the time
    if (e->learn_frames == 0 || e->learn_frames < min_frames || e->learn_no_energy * 20 > e->learn_frames) {
        learn_finish(e, PE_LEARN_NO_DATA);
        return;
    }

    pe_profile_t &p = e->candidate;
    memset(&p, 0, sizeof(p));
    p.magic = PE_PROFILE_MAGIC;
    p.version = PE_PROFILE_VERSION;
    p.size = sizeof(pe_profile_t);
    p.gate_cm = e->learn_gate_cm;
    p.learned = 1;
    p.gates = std::max(e->learn_moving_gates, e->learn_still_gates);
    p.frames = e->learn_frames;
    p.sequence = e->stored.learned ? e->stored.sequence + 1 : 1;
    p.duration_s = duration_s;
    uint8_t channels = 0;
    uint8_t noisy = 0;
    uint8_t hold_high = 0;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            pe_channel_t &ch = p.ch[kind][g];
            pe_compute_channel(&e->p, e->hist[kind][g], e->learn_frames, &ch);
            if ((ch.flags & PE_CH_NO_DATA) || g > max_gate(e, kind)) {
                continue; // not counted with the current Range: does not decide the outcome
            }
            channels++;
            if (ch.trigger >= PE_OFF) {
                noisy++;
            }
            if (ch.flags & PE_CH_HOLD_HIGH) {
                hold_high++;
            }
        }
    }
    p.crc32 = pe_profile_crc(&p);
    e->has_candidate = true;
    e->learn_report.channels = channels;
    e->learn_report.noisy = noisy;
    e->learn_report.hold_high = hold_high;

    if (channels == 0 || noisy == channels) {
        learn_finish(e, PE_LEARN_NO_USABLE);
        return;
    }
    if ((uint32_t)noisy * 100 > (uint32_t)channels * e->p.max_noisy_percent) {
        learn_finish(e, PE_LEARN_TOO_NOISY);
        return;
    }
    // Only now is the old profile replaced, and only once the new one is stored.
    if (e->save && !e->save(&p, e->save_ctx)) {
        learn_finish(e, PE_LEARN_SAVE_FAILED);
        return;
    }
    e->stored = p;
    e->events |= PE_EV_PROFILE;
    learn_finish(e, PE_LEARN_OK);
}

static void learn_progress(pe_engine_t *e, uint32_t now_ms)
{
    if (e->learn_state == PE_LEARN_WAITING) {
        if (now_ms - e->learn_phase_ms < e->p.learn_delay_ms) {
            return;
        }
        e->hist = (uint16_t(*)[PE_GATES][PE_MAX_ENERGY + 1])calloc(PE_KINDS, sizeof(*e->hist));
        if (e->hist == nullptr) {
            learn_finish(e, PE_LEARN_NO_MEMORY);
            return;
        }
        e->learn_state = PE_LEARN_RECORDING;
        e->learn_phase_ms = now_ms;
        e->learn_last_ms = now_ms;
        e->learn_frames = 0;
        e->learn_no_energy = 0;
        e->learn_max_gap_ms = 0;
        e->learn_moving_gates = 0;
        e->learn_still_gates = 0;
        e->learn_gate_cm = e->range.gate_cm;
        e->has_candidate = false;
        e->events |= PE_EV_LEARN_PHASE;
        return;
    }
    if (e->learn_state != PE_LEARN_RECORDING) {
        return;
    }
    // Silence counts from the last report with energies (or the start of the recording)
    if (now_ms - e->learn_last_ms > e->p.learn_max_gap_ms) {
        e->learn_max_gap_ms = std::max(e->learn_max_gap_ms, now_ms - e->learn_last_ms);
        learn_finish(e, PE_LEARN_DATA_GAP);
        return;
    }
    if (now_ms - e->learn_phase_ms >= e->p.learn_duration_ms) {
        learn_analyze(e);
        if (e->learn_report.result == PE_LEARN_OK) {
            select_active(e, now_ms); // the new thresholds from now on
        }
    }
}

uint32_t pe_learn_remaining_ms(const pe_engine_t *e, uint32_t now_ms)
{
    uint32_t phase = 0;
    if (e->learn_state == PE_LEARN_WAITING) {
        phase = e->p.learn_delay_ms;
    } else if (e->learn_state == PE_LEARN_RECORDING) {
        phase = e->p.learn_duration_ms;
    } else {
        return 0;
    }
    const uint32_t elapsed = now_ms - e->learn_phase_ms;
    return elapsed >= phase ? 0 : phase - elapsed;
}

// ---------------------------------------------------------------------------------------------------------------------

void pe_step(pe_engine_t *e, const pe_frame_t *f, uint32_t now_ms)
{
    if (f->engineering) {
        bool ok = f->moving_gates <= PE_GATES && f->still_gates <= PE_GATES;
        for (int g = 0; ok && g < f->moving_gates; g++) {
            ok = f->moving[g] <= PE_MAX_ENERGY;
        }
        for (int g = 0; ok && g < f->still_gates; g++) {
            ok = f->still[g] <= PE_MAX_ENERGY;
        }
        if (!ok) {
            // Damaged report: ignored, it says nothing about the room
            e->frames_bad++;
            return;
        }
    }
    e->any_frame = true;
    e->last_any_ms = now_ms;
    if (!f->engineering) {
        e->frames_no_energy++;
        if (e->learn_state == PE_LEARN_RECORDING) {
            e->learn_no_energy++;
        }
        update_sensor(e, now_ms);
        learn_progress(e, now_ms);
        return;
    }
    e->energy_frame = true;
    e->last_energy_ms = now_ms;
    e->frames_ok++;
    update_sensor(e, now_ms);
    e->last = *f;
    if (e->learn_state == PE_LEARN_RECORDING) {
        learn_record(e, f, now_ms);
    }
    uint16_t trig = 0;
    evaluate(e, f, now_ms, &trig);
    adapt(e, f, now_ms, trig);
    learn_progress(e, now_ms);
}

void pe_tick(pe_engine_t *e, uint32_t now_ms)
{
    update_sensor(e, now_ms);
    check_countdown(e, now_ms);
    learn_progress(e, now_ms);
}

bool pe_occupied_report(const pe_engine_t *e)
{
    return e->presence == PE_PRESENCE_PRESENT && (e->evidence || pe_sensor_fault(e));
}

uint32_t pe_remaining_ms(const pe_engine_t *e, uint32_t now_ms)
{
    if (e->presence != PE_PRESENCE_PRESENT || e->evidence || pe_sensor_fault(e)) {
        return UINT32_MAX;
    }
    const uint32_t elapsed = now_ms - e->last_evidence_ms;
    return elapsed >= e->absence_ms ? 0 : e->absence_ms - elapsed;
}

bool pe_profile_learned(const pe_engine_t *e)
{
    return e->active->learned;
}

uint32_t pe_take_events(pe_engine_t *e)
{
    const uint32_t events = e->events;
    e->events = 0;
    return events;
}

const char *pe_presence_name(pe_presence_t presence)
{
    return presence == PE_PRESENCE_PRESENT ? "present" : "waiting for a strong signal";
}

const char *pe_sensor_name(pe_sensor_t sensor)
{
    switch (sensor) {
    case PE_SENSOR_STARTING:
        return "starting";
    case PE_SENSOR_OK:
        return "OK";
    case PE_SENSOR_NO_DATA:
        return "no data from the radar";
    case PE_SENSOR_NO_ENERGY:
        return "reports without energies (engineering mode off)";
    case PE_SENSOR_CONFIGURING:
        return "radar being configured";
    }
    return "?";
}

const char *pe_learn_state_name(pe_learn_state_t state)
{
    switch (state) {
    case PE_LEARN_IDLE:
        return "idle";
    case PE_LEARN_WAITING:
        return "waiting for the room to be left";
    case PE_LEARN_RECORDING:
        return "recording the empty room";
    }
    return "?";
}

const char *pe_learn_result_name(pe_learn_result_t result)
{
    switch (result) {
    case PE_LEARN_NONE:
        return "none since boot";
    case PE_LEARN_OK:
        return "OK, new thresholds saved";
    case PE_LEARN_CANCELLED:
        return "cancelled";
    case PE_LEARN_NO_MEMORY:
        return "failed: no memory for the recording";
    case PE_LEARN_NO_DATA:
        return "failed: too few reports with energies (radar silent or engineering mode off)";
    case PE_LEARN_DATA_GAP:
        return "failed: the radar went silent or stopped sending energies while recording";
    case PE_LEARN_SETTINGS_CHANGED:
        return "failed: the radar resolution changed while learning";
    case PE_LEARN_TOO_NOISY:
        return "failed: too many gates too noisy (someone moved, or a fan, curtain... in range)";
    case PE_LEARN_NO_USABLE:
        return "failed: no gate in range can trigger";
    case PE_LEARN_SAVE_FAILED:
        return "failed: could not be saved, old thresholds kept";
    }
    return "?";
}

#endif // CONFIG_PRESENCE_ESP_ENGINE
