/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>

// Built only with CONFIG_PRESENCE_ESP_ENGINE (the host tests define it)
#if CONFIG_PRESENCE_ESP_ENGINE

#include <string.h>

#include <algorithm>
#include <initializer_list>

#include "presence_engine.h"

static_assert(PE_GATES <= 16, "gates are bits of a uint16_t");
static_assert(sizeof(pe_noise_t) == 40, "pe_noise_t is stored in NVS: keep its layout");

// LD2410 factory thresholds (HLK-LD2410 serial protocol, table 7), in use until the radar's are read. The still
// thresholds of gates 0 and 1 cannot be set.
static const uint8_t kFactoryMoving[PE_GATES] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kFactoryStill[PE_GATES] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };

// ---------------------------------------------------------------------------------------------------------------------
// Thresholds

void pe_factory_thresholds(pe_radar_thresholds_t *out)
{
    memcpy(out->moving, kFactoryMoving, sizeof(out->moving));
    memcpy(out->still, kFactoryStill, sizeof(out->still));
}

void pe_compute_thresholds(const pe_params_t *p, const pe_radar_thresholds_t *radar, const pe_noise_t *noise,
                           pe_thresholds_t *out)
{
    memset(out, 0, sizeof(*out));
    out->noise = noise != nullptr;
    bool factory = true;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            pe_channel_t &ch = out->ch[kind][g];
            const uint8_t t = kind == PE_MOVING ? radar->moving[g] : radar->still[g];
            ch.radar = t;
            if (kind == PE_STILL && g < PE_FIRST_STILL_GATE) {
                ch.trigger = PE_OFF;
                ch.hold = PE_OFF;
                ch.flags = PE_CH_NOT_SET;
                continue;
            }
            factory = factory && t == (kind == PE_MOVING ? kFactoryMoving[g] : kFactoryStill[g]);
            if (t >= PE_MAX_ENERGY) {
                ch.trigger = PE_OFF;
                ch.hold = PE_OFF;
                ch.flags = PE_CH_IGNORED;
                continue;
            }
            // Strong: above the radar threshold, as the radar counts a target, and above the noise of the empty room
            const uint8_t level = noise ? noise->level[kind][g] : PE_NOISE_UNKNOWN;
            int trigger = t + 1 + p->trigger_margin;
            if (level != PE_NOISE_UNKNOWN && level + p->trigger_noise_margin > trigger) {
                trigger = level + p->trigger_noise_margin;
                ch.flags |= PE_CH_NOISE_TRIGGER;
            }
            ch.trigger = (uint8_t)std::min(trigger, PE_OFF); // above 100: cannot trigger
            // Hold: below the trigger, never 0 (always reached)
            const int highest = ch.trigger == PE_OFF ? PE_MAX_ENERGY : ch.trigger - 1;
            if (level == PE_NOISE_UNKNOWN) {
                // Nothing known about the noise below the radar threshold: no lower than it
                ch.hold = (uint8_t)std::max(1, std::min((int)t, highest));
                ch.flags |= PE_CH_NO_NOISE;
                continue;
            }
            // A share of the threshold lower, at most hold_drop_max, but above the noise of the empty room (else the
            // noise would keep presence; a noise that close to 100 cannot be told from anyone: no hold)
            const int drop = std::min<int>(p->hold_drop_max, (t * p->hold_drop_percent + 50) / 100);
            const int floor = level + p->hold_noise_margin;
            int hold = t - drop;
            if (hold < floor) {
                hold = floor;
                ch.flags |= PE_CH_NOISE_HOLD;
            }
            ch.hold = hold > PE_MAX_ENERGY ? PE_OFF : (uint8_t)std::max(1, std::min(hold, highest));
        }
    }
    out->factory = factory;
}

// ---------------------------------------------------------------------------------------------------------------------
// Noise of the empty room

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

// Noise level of one channel: the 99th percentile, and at least median + 3 robust spreads (1.4826 x median absolute
// deviation, at least 1), which a short recording estimates more steadily than the percentile.
static uint8_t channel_noise(const uint16_t hist[PE_MAX_ENERGY + 1])
{
    uint32_t counts[PE_MAX_ENERGY + 1];
    uint32_t n = 0;
    for (int v = 0; v <= PE_MAX_ENERGY; v++) {
        counts[v] = hist[v];
        n += hist[v];
    }
    if (n == 0) {
        return PE_NOISE_UNKNOWN;
    }
    const uint8_t median = quantile(counts, n, 50);
    uint32_t deviations[PE_MAX_ENERGY + 1] = {};
    for (int v = 0; v <= PE_MAX_ENERGY; v++) {
        deviations[v > median ? v - median : median - v] += counts[v];
    }
    const uint32_t mad = quantile(deviations, n, 50);
    const uint32_t spread_x1000 = std::max<uint32_t>(mad * 1483, 1000);
    const uint32_t robust = (median * 1000 + 3 * spread_x1000 + 999) / 1000;
    const uint32_t level = std::max<uint32_t>(quantile(counts, n, 99), robust);
    return (uint8_t)std::min<uint32_t>(level, PE_MAX_ENERGY);
}

uint32_t pe_noise_crc(const pe_noise_t *noise)
{
    const uint8_t *bytes = (const uint8_t *)noise;
    const size_t len = offsetof(pe_noise_t, crc32);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool pe_noise_valid(const pe_noise_t *noise)
{
    if (noise->magic != PE_NOISE_MAGIC || noise->version != PE_NOISE_VERSION || noise->size != sizeof(pe_noise_t) ||
        noise->crc32 != pe_noise_crc(noise)) {
        return false;
    }
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            const uint8_t level = noise->level[kind][g];
            if (level > PE_MAX_ENERGY && level != PE_NOISE_UNKNOWN) {
                return false;
            }
        }
    }
    return true;
}

void pe_noise_from_histograms(const uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1],
                              const uint16_t still[PE_GATES][PE_MAX_ENERGY + 1], uint32_t frames, uint16_t gate_cm,
                              pe_noise_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = PE_NOISE_MAGIC;
    out->version = PE_NOISE_VERSION;
    out->size = sizeof(pe_noise_t);
    out->gate_cm = gate_cm;
    out->frames = frames;
    for (int g = 0; g < PE_GATES; g++) {
        out->level[PE_MOVING][g] = channel_noise(moving[g]);
        out->level[PE_STILL][g] = channel_noise(still[g]);
    }
    out->crc32 = pe_noise_crc(out);
}

// ---------------------------------------------------------------------------------------------------------------------
// Learning: thresholds from the empty room

void pe_room_from_histograms(const uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1],
                             const uint16_t still[PE_GATES][PE_MAX_ENERGY + 1], const uint8_t moving_max[PE_GATES],
                             const uint8_t still_max[PE_GATES], uint32_t frames, pe_room_t *out)
{
    memset(out, 0, sizeof(*out));
    out->frames = frames;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            const uint16_t *hist = kind == PE_MOVING ? moving[g] : still[g];
            uint64_t n = 0;
            uint64_t sum = 0;
            uint8_t peak = 0;
            for (int v = 0; v <= PE_MAX_ENERGY; v++) {
                n += hist[v];
                sum += (uint64_t)hist[v] * v;
                if (hist[v] > 0) {
                    peak = (uint8_t)v;
                }
            }
            if (n == 0) {
                continue;
            }
            out->valid[kind][g] = true;
            out->avg[kind][g] = (uint8_t)((sum + n / 2) / n);
            out->peak[kind][g] = peak;
            out->max[kind][g] = std::min<uint8_t>(kind == PE_MOVING ? moving_max[g] : still_max[g], peak);
        }
    }
}

void pe_learn_thresholds(const pe_params_t *p, const pe_learn_params_t *lp, const pe_room_t *room,
                         const pe_noise_t *noise, const pe_radar_thresholds_t *current, pe_radar_thresholds_t *out,
                         uint8_t flags[PE_KINDS][PE_GATES])
{
    *out = *current;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            uint8_t f = 0;
            uint8_t &t = kind == PE_MOVING ? out->moving[g] : out->still[g];
            if ((kind == PE_STILL && g < PE_FIRST_STILL_GATE) || !room->valid[kind][g]) {
                f = PE_LEARN_KEPT;
            } else {
                const int max = room->max[kind][g];
                const int avg = room->avg[kind][g];
                int want = max + std::max<int>(lp->min_margin, max - avg);
                const uint8_t level = noise ? noise->level[kind][g] : PE_NOISE_UNKNOWN;
                if (level != PE_NOISE_UNKNOWN && level + p->trigger_noise_margin - 1 > want) {
                    want = level + p->trigger_noise_margin - 1;
                    f |= PE_LEARN_NOISE;
                }
                const int factory = kind == PE_MOVING ? kFactoryMoving[g] : kFactoryStill[g];
                const int floor = (factory * lp->floor_percent + 99) / 100;
                const int ceiling = std::min(factory + lp->max_raise, PE_MAX_ENERGY - 1);
                if (want < floor) {
                    want = floor;
                    f |= PE_LEARN_FLOOR;
                }
                if (want > ceiling) {
                    want = ceiling;
                    f |= PE_LEARN_LIMIT;
                }
                t = (uint8_t)want;
            }
            if (flags) {
                flags[kind][g] = f;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine

void pe_default_params(pe_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->trigger_frames = 6;
    p->trigger_window = 8;
    p->fast_frames = 3;
    p->fast_margin = 20;
    p->hold_frames = 3;
    p->hold_window = 10;
    p->near_frames = 1;
    p->near_gates = 1;
    p->data_timeout_ms = 2000;
    p->trigger_margin = 0;
    p->trigger_noise_margin = 5;
    p->hold_drop_percent = 20;
    p->hold_drop_max = 5;
    p->hold_noise_margin = 2;
}

static void sanitize_params(pe_params_t *p)
{
    p->trigger_window = std::clamp<uint8_t>(p->trigger_window, 1, PE_WINDOW_MAX);
    p->trigger_frames = std::clamp<uint8_t>(p->trigger_frames, 1, p->trigger_window);
    p->fast_frames = std::min<uint8_t>(p->fast_frames, PE_WINDOW_MAX);
    p->hold_window = std::clamp<uint8_t>(p->hold_window, 1, PE_WINDOW_MAX);
    p->hold_frames = std::clamp<uint8_t>(p->hold_frames, 1, p->hold_window);
    p->near_frames = std::min<uint8_t>(p->near_frames, p->hold_window);
    p->near_gates = std::min<uint8_t>(p->near_gates, PE_GATES - 1);
    p->data_timeout_ms = std::max<uint32_t>(p->data_timeout_ms, 300);
    p->hold_drop_percent = std::min<uint8_t>(p->hold_drop_percent, 90);
    p->trigger_noise_margin = std::max<uint8_t>(p->trigger_noise_margin, 1);
    // The hold threshold stays below the trigger
    p->hold_noise_margin = std::min<uint8_t>(p->hold_noise_margin, p->trigger_noise_margin - 1);
}

static void clear_window(pe_engine_t *e)
{
    e->window_count = 0;
    e->window_pos = 0;
    memset(e->trig_bits, 0, sizeof(e->trig_bits));
    memset(e->fast_bits, 0, sizeof(e->fast_bits));
    memset(e->hold_bits, 0, sizeof(e->hold_bits));
    memset(e->recent, 0, sizeof(e->recent));
}

void pe_init(pe_engine_t *e, const pe_params_t *p, uint32_t now_ms)
{
    memset(e, 0, sizeof(*e));
    e->p = *p;
    sanitize_params(&e->p);
    pe_factory_thresholds(&e->radar);
    pe_compute_thresholds(&e->p, &e->radar, nullptr, &e->th);
    e->range = { 0, PE_GATES - 1, PE_GATES - 1 };
    e->absence_ms = 60 * 1000;
    e->sensor = PE_SENSOR_STARTING;
    e->start_ms = now_ms;
    e->presence = PE_PRESENCE_WAITING;
}

// Thresholds in use from the radar's, the noise (when recorded with the gate length in use) and the Range.
static void apply(pe_engine_t *e, bool from_radar, const pe_range_t *range)
{
    const bool noise_usable = e->has_noise && (range->gate_cm == 0 || e->noise.gate_cm == range->gate_cm);
    pe_thresholds_t th;
    pe_compute_thresholds(&e->p, &e->radar, noise_usable ? &e->noise : nullptr, &th);
    th.from_radar = from_radar;
    const bool thresholds_changed = th.from_radar != e->th.from_radar || th.noise != e->th.noise ||
                                    memcmp(th.ch, e->th.ch, sizeof(th.ch)) != 0;
    const bool range_changed = range->gate_cm != e->range.gate_cm ||
                               range->max_moving_gate != e->range.max_moving_gate ||
                               range->max_still_gate != e->range.max_still_gate;
    e->th = th;
    e->range = *range;
    if (thresholds_changed) {
        e->events |= PE_EV_THRESHOLDS;
    }
    if (thresholds_changed || range_changed) {
        // The reports counted so far were judged with the old thresholds or gates. Presence stays as it is.
        clear_window(e);
    }
}

void pe_configure(pe_engine_t *e, const pe_radar_thresholds_t *radar, const pe_range_t *range, uint32_t now_ms)
{
    (void)now_ms;
    pe_range_t r = *range;
    r.max_moving_gate = std::min<uint8_t>(r.max_moving_gate, PE_GATES - 1);
    r.max_still_gate = std::min<uint8_t>(r.max_still_gate, PE_GATES - 1);
    e->radar = *radar;
    apply(e, true, &r);
}

bool pe_set_noise(pe_engine_t *e, const pe_noise_t *noise, uint32_t now_ms)
{
    (void)now_ms;
    if (noise != nullptr && !pe_noise_valid(noise)) {
        return false;
    }
    e->has_noise = noise != nullptr;
    if (noise != nullptr) {
        e->noise = *noise;
    }
    const pe_range_t range = e->range;
    apply(e, e->th.from_radar, &range);
    return true;
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
    const pe_sensor_t silent = e->busy ? PE_SENSOR_BUSY : PE_SENSOR_NO_DATA;
    if (!e->any_frame) {
        return now_ms - e->start_ms >= timeout ? silent : PE_SENSOR_STARTING;
    }
    if (now_ms - e->last_any_ms >= timeout) {
        return silent;
    }
    if (!e->energy_frame || now_ms - e->last_energy_ms >= timeout) {
        // Reports keep coming, but without energies: the engineering mode is off (radar restarted, or its own
        // measurement of the empty room)
        if (!e->energy_frame && now_ms - e->start_ms < timeout) {
            return PE_SENSOR_STARTING;
        }
        return e->busy ? PE_SENSOR_BUSY : PE_SENSOR_NO_ENERGY;
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
    } else if (sensor == PE_SENSOR_OK) {
        // Data back. Nothing is known about the time without data: a present room starts its full countdown again
        // (it is never made empty because the radar was silent).
        if (e->presence == PE_PRESENCE_PRESENT) {
            e->last_evidence_ms = now_ms;
        }
        clear_window(e);
    }
}

void pe_set_busy(pe_engine_t *e, bool busy, uint32_t now_ms)
{
    e->busy = busy;
    update_sensor(e, now_ms);
}

uint8_t pe_hold_threshold(const pe_engine_t *e, int kind, int gate)
{
    return e->th.ch[kind][gate].hold;
}

uint8_t pe_trigger_threshold(const pe_engine_t *e, int kind, int gate)
{
    return e->th.ch[kind][gate].trigger;
}

static uint8_t max_gate(const pe_engine_t *e, int kind)
{
    return kind == PE_MOVING ? e->range.max_moving_gate : e->range.max_still_gate;
}

bool pe_channel_counted(const pe_engine_t *e, int kind, int gate)
{
    return gate <= max_gate(e, kind) && !(e->th.ch[kind][gate].flags & (PE_CH_NOT_SET | PE_CH_IGNORED));
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

// Gate where presence is confirmed (frames reports with bits among the last window, at that gate or a neighbour), -1
// if none.
static int confirmed_gate(const pe_engine_t *e, const uint16_t *bits, int window, int frames)
{
    for (int g = 0; g < PE_GATES; g++) {
        uint16_t mask = (uint16_t)(1u << g);
        if (g > 0) {
            mask |= (uint16_t)(1u << (g - 1));
        }
        if (g < PE_GATES - 1) {
            mask |= (uint16_t)(1u << (g + 1));
        }
        if (count_reports(e, bits, window, mask) >= frames) {
            return g;
        }
    }
    return -1;
}

// The person's gate and near_gates on each side
static uint16_t person_mask(const pe_engine_t *e)
{
    uint16_t mask = 0;
    const int lo = std::max(0, e->person_gate - e->p.near_gates);
    const int hi = std::min(PE_GATES - 1, e->person_gate + e->p.near_gates);
    for (int g = lo; g <= hi; g++) {
        mask |= (uint16_t)(1u << g);
    }
    return mask;
}

// Gate with hold evidence, -1 if none: at the person's gates near_frames reports reaching hold among the last
// hold_window (*near; the gate of the newest one, the closest to the person's), elsewhere hold_frames of them,
// counted per gate (noise reaches hold here and there, a person keeps doing it at the same place).
static int evidence_gate(const pe_engine_t *e, bool *near)
{
    *near = false;
    if (e->p.near_frames > 0) {
        const uint16_t mask = person_mask(e);
        if (count_reports(e, e->hold_bits, e->p.hold_window, mask) >= e->p.near_frames) {
            *near = true;
            const int n = std::min<int>(e->p.hold_window, e->window_count);
            for (int i = 0; i < n; i++) {
                const uint16_t bits = e->hold_bits[(e->window_pos + PE_WINDOW_MAX - i) % PE_WINDOW_MAX] & mask;
                for (int d = 0; bits != 0 && d <= e->p.near_gates; d++) {
                    for (const int g : { e->person_gate - d, e->person_gate + d }) {
                        if (g >= 0 && g < PE_GATES && (bits & (1u << g))) {
                            return g;
                        }
                    }
                }
            }
        }
    }
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

static void go_absent(pe_engine_t *e)
{
    e->presence = PE_PRESENCE_WAITING;
    e->evidence = false;
    clear_window(e);
    e->events |= PE_EV_ABSENT;
}

static void check_countdown(pe_engine_t *e, uint32_t now_ms)
{
    if (e->presence == PE_PRESENCE_PRESENT && !e->evidence && !pe_sensor_fault(e) &&
        now_ms - e->last_evidence_ms >= e->absence_ms) {
        go_absent(e);
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

static void evaluate(pe_engine_t *e, const pe_frame_t *f, uint32_t now_ms)
{
    uint16_t trig = 0;
    uint16_t hold = 0;
    uint16_t fast = 0;
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
    // Strongest channel above its radar threshold in this report (log of the start of presence)
    int best_excess = -1;
    uint8_t best_gate = 0;
    uint8_t best_kind = 0;
    uint8_t best_energy = 0;
    uint8_t best_threshold = 0;
    uint8_t best_strong = 0;
    // Strongest moving channel at its fast level (fast start of presence)
    int fast_excess = -1;
    uint8_t fast_gate = 0;
    uint8_t fast_energy = 0;
    uint8_t fast_threshold = 0;
    uint8_t fast_level = 0;
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
                    best_threshold = e->th.ch[kind][g].radar;
                    best_strong = t;
                }
                const int level = t + e->p.fast_margin;
                if (kind == PE_MOVING && e->p.fast_frames > 0 && level <= PE_MAX_ENERGY && x >= level) {
                    fast |= (uint16_t)(1u << g);
                    if (x - level > fast_excess) {
                        fast_excess = x - level;
                        fast_gate = (uint8_t)g;
                        fast_energy = x;
                        fast_threshold = e->th.ch[kind][g].radar;
                        fast_level = (uint8_t)level;
                    }
                }
            }
            if (x >= pe_hold_threshold(e, kind, g)) {
                hold |= (uint16_t)(1u << g);
            }
        }
    }
    e->trig_bits[e->window_pos] = trig;
    e->hold_bits[e->window_pos] = hold;
    e->fast_bits[e->window_pos] = fast;

    if (e->presence == PE_PRESENCE_WAITING) {
        // A very strong moving energy in fast_frames reports in a row starts presence at once, anything else needs
        // trigger_frames strong reports among the last trigger_window
        int g = -1;
        bool by_fast = false;
        if (e->p.fast_frames > 0) {
            g = confirmed_gate(e, e->fast_bits, e->p.fast_frames, e->p.fast_frames);
            by_fast = g >= 0;
        }
        if (g < 0) {
            g = confirmed_gate(e, e->trig_bits, e->p.trigger_window, e->p.trigger_frames);
        }
        if (g >= 0) {
            e->presence = PE_PRESENCE_PRESENT;
            e->evidence = true;
            e->evidence_gate = (uint8_t)g;
            e->last_evidence_ms = now_ms;
            e->trigger_fast = by_fast && fast_excess >= 0;
            if (e->trigger_fast) {
                e->trigger_gate = fast_gate;
                e->trigger_kind = PE_MOVING;
                e->trigger_energy = fast_energy;
                e->trigger_threshold = fast_threshold;
                e->trigger_strong = fast_level;
            } else {
                e->trigger_gate = best_excess >= 0 ? best_gate : (uint8_t)g;
                e->trigger_kind = best_kind;
                e->trigger_energy = best_energy;
                e->trigger_threshold = best_threshold;
                e->trigger_strong = best_strong;
            }
            e->person_gate = e->trigger_gate;
            e->evidence_near = true;
            e->events |= PE_EV_TRIGGERED | PE_EV_EVIDENCE;
        }
        return;
    }

    // A strong signal confirmed as for a start (someone moving): the person is at its strongest gate now
    if (best_excess >= 0 && ((e->p.fast_frames > 0 && confirmed_gate(e, e->fast_bits, e->p.fast_frames,
                                                                     e->p.fast_frames) >= 0) ||
                             confirmed_gate(e, e->trig_bits, e->p.trigger_window, e->p.trigger_frames) >= 0)) {
        e->person_gate = fast_excess >= 0 ? fast_gate : best_gate;
    }
    bool near = false;
    const int g = evidence_gate(e, &near);
    const bool evidence = g >= 0;
    if (evidence) {
        e->last_evidence_ms = now_ms;
        e->evidence_gate = (uint8_t)g;
        e->evidence_near = near;
    }
    if (evidence != e->evidence) {
        e->evidence = evidence;
        e->events |= PE_EV_EVIDENCE;
    }
    check_countdown(e, now_ms);
}

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
        update_sensor(e, now_ms);
        return;
    }
    e->energy_frame = true;
    e->last_energy_ms = now_ms;
    e->frames_ok++;
    update_sensor(e, now_ms);
    e->last = *f;
    evaluate(e, f, now_ms);
}

void pe_tick(pe_engine_t *e, uint32_t now_ms)
{
    update_sensor(e, now_ms);
    check_countdown(e, now_ms);
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
    case PE_SENSOR_BUSY:
        return "radar being configured or learning the room";
    }
    return "?";
}

#endif // CONFIG_PRESENCE_ESP_ENGINE
