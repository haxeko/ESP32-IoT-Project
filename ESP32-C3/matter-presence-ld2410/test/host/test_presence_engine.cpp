// The ESP32 presence engine (presence_engine.cpp) alone, with simulated reports: per-channel thresholds from the
// radar's (strong above them, hold below them, both above the noise recorded while learning), the radar thresholds
// raised above that noise after a learning, the two-threshold presence with its countdown, sensor faults, the noise
// record, the 32-bit clock wrapping around.
#include "sim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "presence_engine.h"

// ---------------------------------------------------------------------------------------------------------------------
// Noise model of an empty room: per channel a median and a spread (normal noise, rounded, 0-100) and rare spikes.

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (uint32_t)(s >> 11);
    }
    double uniform() { return (next() & 0xFFFFFF) / (double)0x1000000; }
    double gauss()
    {
        const double u1 = std::max(uniform(), 1e-9);
        const double u2 = uniform();
        return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
    }
};

struct Room {
    double median[PE_KINDS][PE_GATES];
    double spread[PE_KINDS][PE_GATES];
    double spike_rate = 0.003; // per channel and report
    int spike_max = 12;
};

// Typical LD2410 empty room: noisy near gates (radar near field), quiet far gates.
static Room typical_room()
{
    Room r;
    const double moving[PE_GATES] = { 28, 22, 12, 8, 6, 4, 3, 3, 2 };
    const double still[PE_GATES] = { 35, 25, 10, 6, 4, 3, 2, 2, 2 };
    for (int g = 0; g < PE_GATES; g++) {
        r.median[PE_MOVING][g] = moving[g];
        r.median[PE_STILL][g] = still[g];
        r.spread[PE_MOVING][g] = 1.0 + moving[g] / 6.0;
        r.spread[PE_STILL][g] = 1.0 + still[g] / 8.0;
    }
    return r;
}

// The room of the HLKRadarTool screenshots in the report (radar at its factory thresholds): still energy of gate 2
// (1.5-2.25 m) 56 on average and at most 63, above its radar threshold 40; gate 3 30 / 34 (threshold 40); moving
// energies 25 / 35 at gate 0 down to 3 / 7 at gate 5. Spreads a little wider than (highest - average) / 3.
static Room reported_room()
{
    Room r;
    const double moving[PE_GATES] = { 25, 11, 6, 4, 4, 3, 3, 3, 2 };
    const double moving_spread[PE_GATES] = { 3.5, 2.5, 2, 2.5, 2, 1.5, 1.5, 1.5, 1.5 };
    const double still[PE_GATES] = { 0, 0, 56, 30, 21, 10, 2, 2, 2 };
    const double still_spread[PE_GATES] = { 1, 1, 2.5, 1.5, 1, 1, 1, 1, 1 };
    for (int g = 0; g < PE_GATES; g++) {
        r.median[PE_MOVING][g] = moving[g];
        r.spread[PE_MOVING][g] = moving_spread[g];
        r.median[PE_STILL][g] = still[g];
        r.spread[PE_STILL][g] = still_spread[g];
    }
    return r;
}

// Thresholds a radar measuring the room itself could set: just above the noise (99th percentile + 5)
static pe_radar_thresholds_t sensitive_thresholds(const Room &room)
{
    pe_radar_thresholds_t t;
    pe_factory_thresholds(&t);
    for (int g = 0; g < PE_GATES; g++) {
        t.moving[g] = (uint8_t)lround(room.median[PE_MOVING][g] + 2.33 * room.spread[PE_MOVING][g] + 5);
        if (g >= PE_FIRST_STILL_GATE) {
            t.still[g] = (uint8_t)lround(room.median[PE_STILL][g] + 2.33 * room.spread[PE_STILL][g] + 5);
        }
    }
    return t;
}

static uint8_t clamp_energy(double v)
{
    return (uint8_t)std::clamp((int)lround(v), 0, PE_MAX_ENERGY);
}

static pe_frame_t room_frame(const Room &room, Rng &rng)
{
    pe_frame_t f = {};
    f.engineering = true;
    f.moving_gates = PE_GATES;
    f.still_gates = PE_GATES;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        uint8_t *e = kind == PE_MOVING ? f.moving : f.still;
        for (int g = 0; g < PE_GATES; g++) {
            double v = room.median[kind][g] + rng.gauss() * room.spread[kind][g];
            if (rng.uniform() < room.spike_rate) {
                v += 4 + rng.next() % (unsigned)room.spike_max;
            }
            e[g] = clamp_energy(v);
        }
    }
    return f;
}

// Person: added on top of the room noise at a gate (moving and/or still energy), with some spread
struct Person {
    int gate = -1;
    int moving = 0;
    int still = 0;
    int spread = 0;
};

static void add_person(pe_frame_t &f, const Person &p, Rng &rng)
{
    if (p.gate < 0) {
        return;
    }
    const int m = p.moving + (p.spread ? (int)lround(rng.gauss() * p.spread) : 0);
    const int s = p.still + (p.spread ? (int)lround(rng.gauss() * p.spread) : 0);
    f.moving[p.gate] = (uint8_t)std::max<int>(f.moving[p.gate], std::clamp(m, 0, 100));
    f.still[p.gate] = (uint8_t)std::max<int>(f.still[p.gate], std::clamp(s, 0, 100));
}

// ---------------------------------------------------------------------------------------------------------------------
// Driving the engine: one report every 100 ms, a tick every 100 ms

struct Bench {
    pe_engine_t e;
    pe_params_t p;
    uint32_t now;
    Rng rng;
    Room room;
    Person person;
    pe_radar_thresholds_t radar;
    pe_range_t range = { 75, 8, 8 };
    bool radar_on = true; // reports arrive
    bool energies = true; // with energies
    std::vector<uint32_t> triggers; // times presence started
    std::vector<uint32_t> absents;  // times presence ended
    uint32_t events = 0;

    explicit Bench(uint32_t start_ms = 1000, uint64_t seed = 1) : now(start_ms), rng(seed), room(typical_room())
    {
        pe_default_params(&p);
        pe_init(&e, &p, now);
        pe_factory_thresholds(&radar);
        configure();
    }

    void configure() { pe_configure(&e, &radar, &range, now); }

    void collect()
    {
        const uint32_t ev = pe_take_events(&e);
        events |= ev;
        if (ev & PE_EV_TRIGGERED) {
            triggers.push_back(now);
        }
        if (ev & PE_EV_ABSENT) {
            absents.push_back(now);
        }
    }

    void run(uint32_t ms)
    {
        for (uint32_t t = 0; t < ms; t += 100) {
            now += 100;
            if (radar_on) {
                pe_frame_t f = room_frame(room, rng);
                add_person(f, person, rng);
                if (!energies) {
                    f.engineering = false;
                    f.moving_gates = 0;
                    f.still_gates = 0;
                }
                pe_step(&e, &f, now);
            }
            pe_tick(&e, now);
            collect();
        }
    }

    // What the ESP32 records while the empty room is learned: `ms` of the room as it is
    pe_noise_t record_noise(uint32_t ms = 30000)
    {
        static uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1];
        static uint16_t still[PE_GATES][PE_MAX_ENERGY + 1];
        memset(moving, 0, sizeof(moving));
        memset(still, 0, sizeof(still));
        uint32_t frames = 0;
        for (uint32_t t = 0; t < ms; t += 100) {
            pe_frame_t f = room_frame(room, rng);
            add_person(f, person, rng);
            for (int g = 0; g < PE_GATES; g++) {
                moving[g][f.moving[g]]++;
                still[g][f.still[g]]++;
            }
            frames++;
        }
        pe_noise_t noise;
        pe_noise_from_histograms(moving, still, frames, range.gate_cm, &noise);
        return noise;
    }

    void learn_noise()
    {
        const pe_noise_t noise = record_noise();
        CHECK(pe_set_noise(&e, &noise, now));
    }

    // What the ESP32 records while the empty room is learned (120 s of the room as it is): the statistics of every
    // channel (average, highest median of 3 reports, highest report) and its noise
    void record_room(pe_room_t *out_room, pe_noise_t *out_noise, uint32_t ms = 120000)
    {
        static uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1];
        static uint16_t still[PE_GATES][PE_MAX_ENERGY + 1];
        memset(moving, 0, sizeof(moving));
        memset(still, 0, sizeof(still));
        uint8_t last[3][PE_KINDS][PE_GATES] = {};
        uint8_t max[PE_KINDS][PE_GATES] = {};
        uint32_t frames = 0;
        for (uint32_t t = 0; t < ms; t += 100) {
            pe_frame_t f = room_frame(room, rng);
            add_person(f, person, rng);
            for (int g = 0; g < PE_GATES; g++) {
                moving[g][f.moving[g]]++;
                still[g][f.still[g]]++;
                for (int kind = 0; kind < PE_KINDS; kind++) {
                    last[2][kind][g] = last[1][kind][g];
                    last[1][kind][g] = last[0][kind][g];
                    last[0][kind][g] = kind == PE_MOVING ? f.moving[g] : f.still[g];
                    if (frames >= 2) {
                        const uint8_t a = last[0][kind][g], b = last[1][kind][g], c = last[2][kind][g];
                        const uint8_t median = std::max(std::min(a, b), std::min(std::max(a, b), c));
                        max[kind][g] = std::max(max[kind][g], median);
                    }
                }
            }
            frames++;
        }
        pe_room_from_histograms(moving, still, max[PE_MOVING], max[PE_STILL], frames, out_room);
        pe_noise_from_histograms(moving, still, frames, range.gate_cm, out_noise);
    }

    // A learning: thresholds set from the room recorded, written to the radar, read back; the noise kept
    void learn_room(const pe_learn_params_t &lp = { 75, 8, 30 })
    {
        pe_room_t r;
        pe_noise_t noise;
        record_room(&r, &noise);
        pe_radar_thresholds_t learned;
        pe_learn_thresholds(&e.p, &lp, &r, &noise, &radar, &learned, nullptr);
        radar = learned;
        configure();
        CHECK(pe_set_noise(&e, &noise, now));
    }
};

// Thresholds a channel should get with the default parameters: strong above T and from noise + 5 (above 100: never);
// hold T - min(10, 40 % of T), at least noise + 2 (above 100: never), below the trigger, at least 1
static int expected_trigger(int t, int level)
{
    return std::min(std::max(t + 1, level + 5), PE_OFF);
}
static int expected_hold(int t, int level)
{
    const int drop = std::min(10, (t * 40 + 50) / 100);
    const int hold = std::max(t - drop, level + 2);
    if (hold > PE_MAX_ENERGY) {
        return PE_OFF;
    }
    return std::max(1, std::min(hold, expected_trigger(t, level) - 1));
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("ESP32 presence engine:\n");

    test_begin("thresholds from the radar: strong above it; no noise recorded: hold at it; still gates 0-1 not set");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_radar_thresholds_t radar;
        pe_factory_thresholds(&radar);
        pe_thresholds_t th;
        pe_compute_thresholds(&p, &radar, nullptr, &th);
        CHECK(th.factory && !th.noise);
        for (int g = 0; g < PE_GATES; g++) {
            const pe_channel_t &m = th.ch[PE_MOVING][g];
            CHECK(m.radar == radar.moving[g] && m.trigger == radar.moving[g] + 1 && m.hold == radar.moving[g]);
            CHECK(m.flags == PE_CH_NO_NOISE);
            const pe_channel_t &s = th.ch[PE_STILL][g];
            if (g < PE_FIRST_STILL_GATE) {
                CHECK(s.flags == PE_CH_NOT_SET && s.trigger == PE_OFF && s.hold == PE_OFF);
            } else {
                CHECK(s.trigger == radar.still[g] + 1 && s.hold == radar.still[g]);
            }
        }
        // Not the factory ones; 100 ignores the gate, as on the radar; 0 never holds an energy of 0
        radar.moving[3] = 31;
        radar.still[5] = 100;
        radar.still[6] = 0;
        radar.still[0] = 77; // not settable on the radar: does not count as a change from the factory ones
        pe_compute_thresholds(&p, &radar, nullptr, &th);
        CHECK(!th.factory);
        CHECK(th.ch[PE_STILL][5].flags == PE_CH_IGNORED && th.ch[PE_STILL][5].trigger == PE_OFF &&
              th.ch[PE_STILL][5].hold == PE_OFF);
        CHECK(th.ch[PE_STILL][6].trigger == 1 && th.ch[PE_STILL][6].hold == 1);
        radar.moving[3] = 30;
        radar.still[5] = 30;
        radar.still[6] = 20;
        pe_compute_thresholds(&p, &radar, nullptr, &th);
        CHECK(th.factory);
    }

    test_begin("hold thresholds with the noise: radar - 40 % (at most 10), above the noise + 2, below the trigger");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_radar_thresholds_t radar = {};
        pe_noise_t noise = {};
        const uint8_t t[PE_GATES] = { 50, 50, 20, 15, 10, 3, 50, 99, 30 };
        const uint8_t level[PE_GATES] = { 3, 45, 3, 3, 3, 3, 60, 1, PE_NOISE_UNKNOWN };
        for (int g = 0; g < PE_GATES; g++) {
            radar.moving[g] = t[g];
            noise.level[PE_MOVING][g] = level[g];
            noise.level[PE_STILL][g] = 0;
        }
        pe_thresholds_t th;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.noise);
        // Gates 5 and 6: the noise reaches the radar threshold, strong from noise + 5, hold just below
        const int want[PE_GATES] = { 40, 47, 12, 9, 6, 5, 62, 89, 30 };
        const int trigger[PE_GATES] = { 51, 51, 21, 16, 11, 8, 65, 100, 31 };
        for (int g = 0; g < PE_GATES; g++) {
            const pe_channel_t &ch = th.ch[PE_MOVING][g];
            CHECK(ch.hold == want[g] && ch.trigger == trigger[g]);
            if (level[g] != PE_NOISE_UNKNOWN) {
                CHECK(ch.hold == expected_hold(t[g], level[g]) && ch.trigger == expected_trigger(t[g], level[g]));
            }
        }
        CHECK(th.ch[PE_MOVING][0].flags == 0 && th.ch[PE_MOVING][1].flags == PE_CH_NOISE_HOLD);
        CHECK(th.ch[PE_MOVING][5].flags == (PE_CH_NOISE_TRIGGER | PE_CH_NOISE_HOLD));
        CHECK(th.ch[PE_MOVING][6].flags == (PE_CH_NOISE_TRIGGER | PE_CH_NOISE_HOLD));
        CHECK(th.ch[PE_MOVING][8].flags == PE_CH_NO_NOISE);
        // Other settings: no drop at all, or a larger one
        p.hold_drop_percent = 0;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.ch[PE_MOVING][2].hold == 20);
        p.hold_drop_percent = 60;
        p.hold_drop_max = 15;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.ch[PE_MOVING][0].hold == 35 && th.ch[PE_MOVING][2].hold == 8);
    }

    test_begin("noise reaching the radar threshold (report: still gate 2 noise 64, radar 40): strong from noise + 5, "
               "hold noise + 2; noise near 100: never; extra margin on every gate");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_radar_thresholds_t radar;
        pe_factory_thresholds(&radar);
        pe_noise_t noise = {};
        memset(noise.level, 3, sizeof(noise.level));
        noise.level[PE_STILL][2] = 64;  // radar 40
        noise.level[PE_STILL][3] = 36;  // radar 40: 36 + 5 = 41, as the radar counts
        noise.level[PE_STILL][4] = 27;  // radar 30: 27 + 5 = 32, one above
        noise.level[PE_MOVING][8] = 96; // cannot start presence, holds from 98
        noise.level[PE_MOVING][7] = 99; // neither
        pe_thresholds_t th;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        const pe_channel_t &s2 = th.ch[PE_STILL][2];
        CHECK(s2.radar == 40 && s2.trigger == 69 && s2.hold == 66);
        CHECK(s2.flags == (PE_CH_NOISE_TRIGGER | PE_CH_NOISE_HOLD));
        const pe_channel_t &s3 = th.ch[PE_STILL][3];
        CHECK(s3.trigger == 41 && s3.hold == 38 && s3.flags == PE_CH_NOISE_HOLD);
        const pe_channel_t &s4 = th.ch[PE_STILL][4];
        CHECK(s4.trigger == 32 && s4.hold == 29 && s4.flags == (PE_CH_NOISE_TRIGGER | PE_CH_NOISE_HOLD));
        CHECK(th.ch[PE_MOVING][8].trigger == PE_OFF && th.ch[PE_MOVING][8].hold == 98);
        CHECK(th.ch[PE_MOVING][7].trigger == PE_OFF && th.ch[PE_MOVING][7].hold == PE_OFF);
        CHECK(th.ch[PE_MOVING][0].trigger == 51 && th.ch[PE_MOVING][0].hold == 40 && th.ch[PE_MOVING][0].flags == 0);
        // Extra margin on every gate; the hold does not move
        p.trigger_margin = 4;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.ch[PE_MOVING][0].trigger == 55 && th.ch[PE_MOVING][0].hold == 40 && th.ch[PE_MOVING][0].flags == 0);
        CHECK(th.ch[PE_STILL][3].trigger == 45 && th.ch[PE_STILL][3].hold == 38);
        CHECK(th.ch[PE_STILL][2].trigger == 69 && th.ch[PE_STILL][2].hold == 66);
        pe_compute_thresholds(&p, &radar, nullptr, &th);
        CHECK(th.ch[PE_MOVING][0].trigger == 55 && th.ch[PE_MOVING][0].hold == 50);
        CHECK(th.ch[PE_STILL][2].trigger == 45 && th.ch[PE_STILL][2].hold == 40);
        // Settings checked by the engine: the hold margin stays below the trigger margin
        pe_default_params(&p);
        p.trigger_noise_margin = 0;
        pe_engine_t e;
        pe_init(&e, &p, 0);
        CHECK(e.p.trigger_noise_margin == 1 && e.p.hold_noise_margin == 0);
        p.trigger_noise_margin = 2;
        pe_init(&e, &p, 0);
        CHECK(e.p.trigger_noise_margin == 2 && e.p.hold_noise_margin == 1);
    }

    test_begin("any radar threshold, noise and margins: strong above both, hold below the trigger, never 0");
    {
        Rng rng(5);
        for (int i = 0; i < 20000; i++) {
            pe_params_t p;
            pe_default_params(&p);
            p.trigger_margin = (uint8_t)(rng.next() % 31);
            p.trigger_noise_margin = (uint8_t)(rng.next() % 31);
            p.hold_drop_percent = (uint8_t)(rng.next() % 91);
            p.hold_drop_max = (uint8_t)(rng.next() % 51);
            pe_engine_t e;
            pe_init(&e, &p, 0);
            const pe_params_t &q = e.p;
            pe_radar_thresholds_t radar;
            pe_noise_t noise = {};
            for (int g = 0; g < PE_GATES; g++) {
                radar.moving[g] = (uint8_t)(rng.next() % 101);
                radar.still[g] = (uint8_t)(rng.next() % 101);
                noise.level[PE_MOVING][g] = rng.next() % 8 == 0 ? PE_NOISE_UNKNOWN : (uint8_t)(rng.next() % 101);
                noise.level[PE_STILL][g] = rng.next() % 8 == 0 ? PE_NOISE_UNKNOWN : (uint8_t)(rng.next() % 101);
            }
            pe_thresholds_t th;
            pe_compute_thresholds(&q, &radar, &noise, &th);
            for (int kind = 0; kind < PE_KINDS; kind++) {
                for (int g = kind == PE_STILL ? PE_FIRST_STILL_GATE : 0; g < PE_GATES; g++) {
                    const pe_channel_t &ch = th.ch[kind][g];
                    const int t = ch.radar;
                    const int level = noise.level[kind][g];
                    if (t >= PE_MAX_ENERGY) {
                        CHECK(ch.trigger == PE_OFF && ch.hold == PE_OFF);
                        continue;
                    }
                    const int noise_trigger = std::min(level + q.trigger_noise_margin, PE_OFF);
                    CHECK(ch.trigger == std::min(t + 1 + q.trigger_margin, PE_OFF) ||
                          (level != PE_NOISE_UNKNOWN && ch.trigger == noise_trigger));
                    CHECK(ch.trigger >= std::min(t + 1 + q.trigger_margin, PE_OFF));
                    // Below the trigger; radar threshold 0: both 1 (an energy of 0 never counts)
                    CHECK(ch.hold >= 1 && ch.hold <= PE_OFF);
                    CHECK(ch.hold < ch.trigger || ch.trigger == PE_OFF || (ch.trigger == 1 && ch.hold == 1));
                    if (level != PE_NOISE_UNKNOWN) {
                        CHECK(ch.trigger >= std::min(level + q.trigger_noise_margin, PE_OFF));
                        CHECK(ch.hold >= std::min(level + q.hold_noise_margin, PE_OFF));
                    } else {
                        CHECK(ch.hold == t || (t == 0 && ch.hold == 1));
                    }
                }
            }
        }
    }

    test_begin("learned thresholds with the numbers of the HLKRadarTool screenshots (average / highest): max + the "
               "larger of 8 and (max - average), at least 75 % of the factory thresholds");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_room_t room = {};
        const uint8_t moving_max[PE_GATES] = { 26, 17, 8, 9, 6, 7, 4, 7, 5 };
        const uint8_t moving_avg[PE_GATES] = { 13, 7, 2, 4, 2, 4, 2, 3, 3 };
        const uint8_t still_max[PE_GATES] = { 0, 0, 12, 12, 6, 4, 3, 3, 3 };
        const uint8_t still_avg[PE_GATES] = { 0, 0, 3, 4, 3, 2, 2, 2, 2 };
        for (int g = 0; g < PE_GATES; g++) {
            room.max[PE_MOVING][g] = room.peak[PE_MOVING][g] = moving_max[g];
            room.avg[PE_MOVING][g] = moving_avg[g];
            room.max[PE_STILL][g] = room.peak[PE_STILL][g] = still_max[g];
            room.avg[PE_STILL][g] = still_avg[g];
            room.valid[PE_MOVING][g] = room.valid[PE_STILL][g] = true;
        }
        pe_radar_thresholds_t current;
        pe_factory_thresholds(&current);
        current.still[0] = 21; // what the app shows there: not settable, kept
        current.still[1] = 24;
        const pe_learn_params_t lp = { 75, 8, 30 };
        pe_radar_thresholds_t out;
        uint8_t flags[PE_KINDS][PE_GATES];
        pe_learn_thresholds(&p, &lp, &room, nullptr, &current, &out, flags);
        const uint8_t want_moving[PE_GATES] = { 39, 38, 30, 23, 15, 15, 12, 15, 13 };
        const uint8_t want_still[PE_GATES] = { 21, 24, 30, 30, 23, 23, 15, 15, 15 };
        CHECK(memcmp(out.moving, want_moving, PE_GATES) == 0 && memcmp(out.still, want_still, PE_GATES) == 0);
        CHECK(flags[PE_MOVING][0] == 0 && flags[PE_MOVING][1] == PE_LEARN_FLOOR && flags[PE_MOVING][6] == 0);
        CHECK(flags[PE_STILL][0] == PE_LEARN_KEPT && flags[PE_STILL][2] == PE_LEARN_FLOOR);
        // Floor 100 %: never below the factory thresholds (here: the factory thresholds themselves)
        const pe_learn_params_t never_lower = { 100, 8, 30 };
        pe_learn_thresholds(&p, &never_lower, &room, nullptr, &current, &out, nullptr);
        const uint8_t factory_moving[PE_GATES] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
        CHECK(memcmp(out.moving, factory_moving, PE_GATES) == 0 && out.still[2] == 40 && out.still[8] == 20);
        // Floor 0: the room alone
        const pe_learn_params_t room_only = { 0, 8, 30 };
        pe_learn_thresholds(&p, &room_only, &room, nullptr, &current, &out, nullptr);
        CHECK(out.moving[1] == 27 && out.moving[2] == 16 && out.still[2] == 21 && out.still[8] == 11);
    }

    test_begin("learned thresholds: at least the noise + 4, at most factory + 30 (and 99), kept where nothing was "
               "recorded");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_room_t room = {};
        for (int kind = 0; kind < PE_KINDS; kind++) {
            for (int g = 0; g < PE_GATES; g++) {
                room.valid[kind][g] = true;
                room.avg[kind][g] = 3;
                room.max[kind][g] = room.peak[kind][g] = 6;
            }
        }
        room.max[PE_STILL][2] = 85; // something moved there: 85 + 82 limited to 40 + 30
        room.valid[PE_MOVING][7] = false; // no reports: kept
        pe_noise_t noise = {};
        memset(noise.level, PE_NOISE_UNKNOWN, sizeof(noise.level));
        noise.level[PE_STILL][4] = 30; // the robust noise above max + 8: 30 + 4
        pe_radar_thresholds_t current;
        pe_factory_thresholds(&current);
        current.moving[7] = 44;
        const pe_learn_params_t lp = { 75, 8, 30 };
        pe_radar_thresholds_t out;
        uint8_t flags[PE_KINDS][PE_GATES];
        pe_learn_thresholds(&p, &lp, &room, &noise, &current, &out, flags);
        CHECK(out.still[2] == 70 && flags[PE_STILL][2] == PE_LEARN_LIMIT);
        CHECK(out.still[4] == 34 && flags[PE_STILL][4] == PE_LEARN_NOISE);
        CHECK(out.moving[7] == 44 && flags[PE_MOVING][7] == PE_LEARN_KEPT);
        CHECK(out.moving[8] == 14 && flags[PE_MOVING][8] == 0); // 6 + 8, floor 12
        CHECK(out.moving[0] == 38 && flags[PE_MOVING][0] == PE_LEARN_FLOOR);
        const pe_learn_params_t no_limit = { 75, 8, 100 }; // at most 99, never 100 (the gate would be ignored)
        pe_learn_thresholds(&p, &no_limit, &room, &noise, &current, &out, flags);
        CHECK(out.still[2] == 99 && flags[PE_STILL][2] == PE_LEARN_LIMIT);
        // Learning again from the thresholds it set gives the same ones
        pe_radar_thresholds_t again;
        pe_learn_thresholds(&p, &lp, &room, &noise, &current, &out, nullptr);
        pe_learn_thresholds(&p, &lp, &room, &noise, &out, &again, nullptr);
        CHECK(memcmp(&out, &again, sizeof(out)) == 0);
        // The radar held other thresholds (HLKRadarTool, an earlier firmware): floor and ceiling from the factory
        // thresholds all the same
        pe_radar_thresholds_t other = current;
        other.moving[3] = 60; // factory 30: floor 23
        other.moving[5] = 5;  // factory 15: ceiling 45
        room.max[PE_MOVING][5] = 60;
        pe_learn_thresholds(&p, &lp, &room, &noise, &other, &out, flags);
        CHECK(out.moving[3] == 23 && flags[PE_MOVING][3] == PE_LEARN_FLOOR);
        CHECK(out.moving[5] == 45 && flags[PE_MOVING][5] == PE_LEARN_LIMIT);
    }

    test_begin("room statistics from the histograms: rounded average, highest report, highest filtered energy");
    {
        static uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1];
        static uint16_t still[PE_GATES][PE_MAX_ENERGY + 1];
        memset(moving, 0, sizeof(moving));
        memset(still, 0, sizeof(still));
        moving[0][10] = 9; // 9 x 10 and one 60: average 15, highest report 60
        moving[0][60] = 1;
        still[3][4] = 1; // 4 and 5: average 4.5 -> 5
        still[3][5] = 1;
        uint8_t moving_max[PE_GATES] = {};
        uint8_t still_max[PE_GATES] = {};
        moving_max[0] = 10; // the 60 stood alone
        still_max[3] = 9;   // never above the highest report
        pe_room_t room;
        pe_room_from_histograms(moving, still, moving_max, still_max, 10, &room);
        CHECK(room.valid[PE_MOVING][0] && room.avg[PE_MOVING][0] == 15 && room.peak[PE_MOVING][0] == 60);
        CHECK(room.max[PE_MOVING][0] == 10 && room.avg[PE_STILL][3] == 5 && room.max[PE_STILL][3] == 5);
        CHECK(!room.valid[PE_MOVING][1] && room.frames == 10);
    }

    test_begin("noise of a recording: 99th percentile, at least median + 3 spreads; record checked by its CRC");
    {
        static uint16_t moving[PE_GATES][PE_MAX_ENERGY + 1];
        static uint16_t still[PE_GATES][PE_MAX_ENERGY + 1];
        memset(moving, 0, sizeof(moving));
        memset(still, 0, sizeof(still));
        moving[0][10] = 300; // constant 10: median 10, spread at least 1 -> 13
        moving[1][10] = 100; // 10, 12, 14: median 12, MAD 2, spread 2.966 -> 21 (99th percentile 14)
        moving[1][12] = 100;
        moving[1][14] = 100;
        moving[2][5] = 290; // 290 at 5 and 10 bursts at 40: 99th percentile 40
        moving[2][40] = 10;
        // gate 3: nothing recorded
        pe_noise_t n;
        pe_noise_from_histograms(moving, still, 300, 75, &n);
        CHECK(n.level[PE_MOVING][0] == 13 && n.level[PE_MOVING][1] == 21 && n.level[PE_MOVING][2] == 40);
        CHECK(n.level[PE_MOVING][3] == PE_NOISE_UNKNOWN && n.gate_cm == 75 && n.frames == 300);
        CHECK(pe_noise_valid(&n));
        pe_noise_t bad = n;
        bad.level[PE_MOVING][4] ^= 1;
        CHECK(!pe_noise_valid(&bad));
        bad = n;
        bad.version = 2;
        bad.crc32 = pe_noise_crc(&bad);
        CHECK(!pe_noise_valid(&bad));
        bad = n;
        bad.level[PE_STILL][4] = 150;
        bad.crc32 = pe_noise_crc(&bad);
        CHECK(!pe_noise_valid(&bad));
        Bench b;
        CHECK(!pe_set_noise(&b.e, &bad, b.now) && !b.e.has_noise);
        CHECK(pe_set_noise(&b.e, &n, b.now) && b.e.has_noise && b.e.th.noise);
        CHECK(pe_set_noise(&b.e, nullptr, b.now) && !b.e.has_noise && !b.e.th.noise);
    }

    test_begin("boot: factory thresholds until the radar's are read, waiting for a strong signal, sensor starting");
    {
        pe_params_t p;
        pe_default_params(&p);
        pe_engine_t e;
        pe_init(&e, &p, 1000);
        CHECK(!e.th.from_radar && e.th.factory && e.presence == PE_PRESENCE_WAITING);
        CHECK(e.sensor == PE_SENSOR_STARTING && !pe_occupied_report(&e));
        pe_radar_thresholds_t radar;
        pe_factory_thresholds(&radar);
        radar.moving[4] = 25;
        const pe_range_t range = { 75, 8, 8 };
        pe_configure(&e, &radar, &range, 1000);
        CHECK(e.th.from_radar && !e.th.factory && pe_trigger_threshold(&e, PE_MOVING, 4) == 26);
        CHECK(pe_take_events(&e) & PE_EV_THRESHOLDS);
        pe_configure(&e, &radar, &range, 1000); // the same again: nothing new
        CHECK(!(pe_take_events(&e) & PE_EV_THRESHOLDS));
    }

    test_begin("empty room, factory thresholds and noise recorded: 30 min of noise and spikes, never present");
    {
        Bench b;
        b.learn_noise();
        b.run(30 * 60 * 1000);
        CHECK(b.triggers.empty() && b.e.presence == PE_PRESENCE_WAITING && b.e.sensor == PE_SENSOR_OK);
    }

    test_begin("empty room, sensitive radar thresholds (just above the noise): 30 min, never present");
    {
        Bench b(1000, 7);
        b.radar = sensitive_thresholds(b.room);
        b.configure();
        b.learn_noise();
        b.run(30 * 60 * 1000);
        CHECK(b.triggers.empty());
    }

    test_begin("room of the report (still gate 2 noise 56 / 63, radar 40), radar thresholds as they are: without the "
               "noise of the empty room presence starts at once; with it, 30 min never present");
    {
        Bench b(1000, 21);
        b.room = reported_room();
        b.run(10 * 1000);
        CHECK(!b.triggers.empty() && b.e.trigger_gate == 2 && b.e.trigger_kind == PE_STILL); // the false trigger
        Bench n(1000, 22);
        n.room = reported_room();
        n.learn_noise();
        CHECK(pe_trigger_threshold(&n.e, PE_STILL, 2) >= 66 && pe_trigger_threshold(&n.e, PE_STILL, 2) <= 70);
        CHECK(pe_trigger_threshold(&n.e, PE_STILL, 3) == 41 && pe_trigger_threshold(&n.e, PE_MOVING, 0) == 51);
        n.run(30 * 60 * 1000);
        CHECK(n.triggers.empty() && n.e.presence == PE_PRESENCE_WAITING);
    }

    test_begin("room of the report learned by the ESP32 (120 s): still gate 2 set above its noise, 30 min never "
               "present; someone sitting at gate 2 or walking at gate 3 is present, empty 60 s after leaving");
    {
        for (int seed = 0; seed < 5; seed++) {
            Bench b(1000, 30 + seed);
            b.room = reported_room();
            b.learn_room();
            CHECK(b.radar.still[2] >= 65 && b.radar.still[2] <= 70);
            CHECK(!(b.e.th.ch[PE_STILL][2].flags & PE_CH_NOISE_TRIGGER));
            b.run(30 * 60 * 1000);
            CHECK(b.triggers.empty());
            b.person = { 2, 10, 85, 3 }; // sits down at 1.5-2.25 m
            b.run(3000);
            CHECK(b.triggers.size() == 1);
            b.person = { 2, 4, 72, 3 }; // keeps still: weaker, above the hold (noise + 2)
            b.run(10 * 60 * 1000);
            CHECK(b.absents.empty());
            b.person = {};
            const uint32_t gone = b.now;
            b.run(70 * 1000);
            CHECK(b.absents.size() == 1 && b.absents[0] - gone >= 60000 && b.absents[0] - gone <= 61500);
            b.person = { 3, 60, 35, 5 }; // walks in at 2.25-3 m
            b.run(3000);
            CHECK(b.triggers.size() == 2);
        }
    }

    test_begin("typical room learned by the ESP32: every threshold from 75 % of factory to factory + 30; 20 rooms, "
               "30 min never present, a still person far away held");
    {
        for (int seed = 0; seed < 20; seed++) {
            Bench b(1000, 200 + seed);
            const pe_radar_thresholds_t was = b.radar;
            b.learn_room();
            for (int kind = 0; kind < PE_KINDS; kind++) {
                for (int g = kind == PE_STILL ? PE_FIRST_STILL_GATE : 0; g < PE_GATES; g++) {
                    const int f = kind == PE_MOVING ? was.moving[g] : was.still[g];
                    const int t = kind == PE_MOVING ? b.radar.moving[g] : b.radar.still[g];
                    CHECK(t >= (f * 75 + 99) / 100 && t <= f + 30);
                }
            }
            b.run(30 * 60 * 1000);
            CHECK(b.triggers.empty());
            b.person = { 7, 60, 30, 0 }; // walks in far away, then sits still (weak still energy around 14)
            b.run(3000);
            CHECK(b.triggers.size() == 1);
            b.person = { 7, 3, 14, 4 };
            b.run(20 * 60 * 1000);
            CHECK(b.absents.empty());
        }
    }

    test_begin("single and double strong reports (noise bursts) never start presence");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        for (int burst = 1; burst <= 2; burst++) {
            b.person = { 3, 90, 0, 0 };
            b.run(burst * 100);
            b.person = {};
            b.run(3000);
        }
        CHECK(b.triggers.empty());
    }

    test_begin("someone walking in (moving energy 70, fast level 21 + 20): fast start after 4 reports (about 0.4 s), "
               "the log names the gate");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        const uint32_t in = b.now;
        b.person = { 4, 70, 30, 0 };
        b.run(1000);
        // The first report is filtered away, the next three are at the fast level: present at the fourth
        CHECK(b.triggers.size() == 1 && b.triggers[0] - in == 400);
        CHECK(b.e.trigger_gate == 4 && b.e.trigger_kind == PE_MOVING && b.e.trigger_threshold == 20);
        CHECK(b.e.trigger_fast && b.e.trigger_strong == 41);
        CHECK(pe_occupied_report(&b.e));
    }

    test_begin("strong but not very strong (moving 35 at gate 4: strong 21, fast 41): 6 of 8 reports, about 0.7 s");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        const uint32_t in = b.now;
        b.person = { 4, 35, 0, 0 };
        b.run(1000);
        // Filtered strong from the second report on: the sixth strong one is the seventh report
        CHECK(b.triggers.size() == 1 && b.triggers[0] - in == 700);
        CHECK(!b.e.trigger_fast && b.e.trigger_strong == 21 && b.e.trigger_energy == 35);
    }

    test_begin("bursts: 5 strong reports (0.5 s) never start presence; 2 very strong reports never; 3 very strong in a "
               "row do (fast); very strong reports with a gap of two do not");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        for (int i = 0; i < 20; i++) {
            b.person = { 3, 45, 0, 0 }; // gate 3: strong (31), not fast (51)
            b.run(500);
            b.person = {};
            b.run(3000);
        }
        CHECK(b.triggers.empty());
        for (int i = 0; i < 20; i++) {
            b.person = { 3, 95, 0, 0 };
            b.run(200);
            b.person = {};
            b.run(3000);
        }
        CHECK(b.triggers.empty());
        // Very strong in reports 1, 2, 5, 6 (a gap of two): filtered fast in 2, 3, 6, 7, never 3 in a row; strong in
        // 4 of 8 only. (A gap of one report is filled by the median filter: that counts as in a row.)
        for (int r = 1; r <= 6; r++) {
            b.person = (r == 3 || r == 4) ? Person{} : Person{ 3, 95, 0, 0 };
            b.run(100);
        }
        b.person = {};
        b.run(3000);
        CHECK(b.triggers.empty());
        const uint32_t in = b.now;
        b.person = { 3, 95, 0, 0 };
        b.run(300);
        b.person = {};
        b.run(1000);
        CHECK(b.triggers.size() == 1 && b.triggers[0] - in == 400 && b.e.trigger_fast);
    }

    test_begin("fast start: only moving energy; fast start off (0 reports): very strong moving energy needs 6 of 8");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        uint32_t in = b.now;
        b.person = { 4, 0, 95, 0 }; // still energy, however strong: 6 of 8
        b.run(1000);
        CHECK(b.triggers.size() == 1 && b.triggers[0] - in == 700 && !b.e.trigger_fast);
        Bench o;
        o.p.fast_frames = 0;
        pe_init(&o.e, &o.p, o.now);
        o.configure();
        o.learn_noise();
        o.run(5000);
        in = o.now;
        o.person = { 4, 95, 0, 0 };
        o.run(1000);
        CHECK(o.triggers.size() == 1 && o.triggers[0] - in == 700 && !o.e.trigger_fast);
        // Fast level above 100 (strong 90 + 20): that channel has no fast start
        Bench h;
        h.radar.moving[4] = 89;
        h.configure();
        h.learn_noise();
        h.run(5000);
        in = h.now;
        h.person = { 4, 100, 0, 0 };
        h.run(1000);
        CHECK(h.triggers.size() == 1 && h.triggers[0] - in == 700 && !h.e.trigger_fast);
    }

    test_begin("someone walking across two gates (one, then the other, report after report): counted together");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        for (int i = 0; i < 10 && b.triggers.empty(); i++) {
            b.person = { 3 + i % 2, 70, 0, 0 }; // each gate strong in every other report only
            b.run(100);
        }
        CHECK(b.triggers.size() == 1);
    }

    test_begin("a weak signal (above hold, not above the radar threshold) never starts presence");
    {
        Bench b;
        b.learn_noise();
        b.person = { 6, 0, 18, 0 }; // still gate 6: radar 20, hold 12
        b.run(10 * 60 * 1000);
        CHECK(b.triggers.empty());
        b.person = { 6, 0, 20, 0 }; // at the threshold, not above it
        b.run(60 * 1000);
        CHECK(b.triggers.empty());
        b.person = { 6, 0, 21, 0 };
        b.run(2000);
        CHECK(b.triggers.size() == 1);
    }

    test_begin("someone sitting still far away (still energy 14, radar 20, hold 12) stays present; empty 60 s after "
               "leaving");
    {
        Bench b;
        b.learn_noise();
        CHECK(pe_hold_threshold(&b.e, PE_STILL, 7) == 12);
        b.run(5000);
        b.person = { 7, 60, 30, 0 }; // walks in
        b.run(3000);
        CHECK(b.triggers.size() == 1);
        b.person = { 7, 3, 14, 4 }; // sits still: weak still energy, around 14
        b.run(20 * 60 * 1000);
        CHECK(b.absents.empty() && b.e.presence == PE_PRESENCE_PRESENT);
        b.person = {};
        const uint32_t left = b.now;
        b.run(70 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] - left >= 60000 && b.absents[0] - left <= 61500);
        CHECK(!pe_occupied_report(&b.e));
    }

    test_begin("without a noise recording the hold is the radar threshold: the same person is lost");
    {
        Bench b;
        CHECK(pe_hold_threshold(&b.e, PE_STILL, 7) == 20);
        b.run(5000);
        b.person = { 7, 60, 30, 0 };
        b.run(3000);
        b.person = { 7, 3, 14, 4 };
        b.run(5 * 60 * 1000);
        CHECK(!b.absents.empty()); // reported empty while still there (at times present again)
    }

    test_begin("countdown starts again when the hold signal comes back");
    {
        Bench b;
        b.learn_noise();
        b.person = { 3, 70, 40, 0 };
        b.run(3000);
        b.person = {};
        b.run(40 * 1000); // 40 s of the 60 s countdown
        CHECK(b.absents.empty() && pe_remaining_ms(&b.e, b.now) <= 21000);
        b.person = { 3, 0, 35, 0 }; // still signal above hold 30 (radar 40), below the threshold
        b.run(5000);
        CHECK(b.e.evidence && pe_remaining_ms(&b.e, b.now) == UINT32_MAX && pe_occupied_report(&b.e));
        b.person = {};
        const uint32_t gone = b.now;
        b.run(70 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] - gone >= 60000 && b.absents[0] - gone <= 61500);
    }

    test_begin("hold time from Occupancy Sensing (30 s)");
    {
        Bench b;
        b.learn_noise();
        pe_set_absence_time(&b.e, 30 * 1000);
        b.person = { 2, 80, 50, 0 };
        b.run(3000);
        b.person = {};
        const uint32_t gone = b.now;
        b.run(40 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] - gone >= 30000 && b.absents[0] - gone <= 31500);
    }

    test_begin("sensitive radar thresholds: the noise keeps the hold above it, 20 rooms empty again 60-62 s after "
               "the person left");
    {
        for (int room = 0; room < 20; room++) {
            Bench b(1000 + room, 100 + room);
            b.radar = sensitive_thresholds(b.room);
            b.configure();
            b.learn_noise();
            b.run(2000);
            b.person = { room % 9, 70, 35, 5 };
            b.run(10 * 1000);
            b.person = {};
            const uint32_t gone = b.now;
            b.run(3 * 60 * 1000);
            CHECK(b.absents.size() == 1 && b.absents[0] - gone >= 60000 && b.absents[0] - gone <= 62000);
        }
    }

    test_begin("radar silent while present: stays present (no data is no empty room), full countdown once back");
    {
        Bench b;
        b.learn_noise();
        b.person = { 3, 70, 40, 0 };
        b.run(3000);
        b.person = {};
        b.run(30 * 1000);
        b.radar_on = false;
        b.run(10 * 60 * 1000);
        CHECK(b.absents.empty() && b.e.sensor == PE_SENSOR_NO_DATA && pe_occupied_report(&b.e));
        CHECK(pe_remaining_ms(&b.e, b.now) == UINT32_MAX);
        b.radar_on = true;
        const uint32_t back = b.now;
        b.run(70 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] - back >= 60000 && b.absents[0] - back <= 61500);
    }

    test_begin("radar silent while empty: fault reported, nothing invented; reports without energies: fault too; "
               "busy while learning or configuring");
    {
        Bench b;
        b.run(5000);
        b.radar_on = false;
        b.run(3000);
        CHECK(b.e.sensor == PE_SENSOR_NO_DATA && b.e.presence == PE_PRESENCE_WAITING && b.triggers.empty());
        pe_set_busy(&b.e, true, b.now);
        CHECK(b.e.sensor == PE_SENSOR_BUSY);
        pe_set_busy(&b.e, false, b.now);
        b.radar_on = true;
        b.energies = false;
        b.run(3000);
        CHECK(b.e.sensor == PE_SENSOR_NO_ENERGY && pe_sensor_fault(&b.e));
        pe_set_busy(&b.e, true, b.now);
        CHECK(b.e.sensor == PE_SENSOR_BUSY);
        pe_set_busy(&b.e, false, b.now);
        b.energies = true;
        b.run(1000);
        CHECK(b.e.sensor == PE_SENSOR_OK && b.triggers.empty());
    }

    test_begin("damaged reports (energy above 100) are ignored and counted, never taken as presence or absence");
    {
        Bench b;
        b.run(2000);
        pe_frame_t f = {};
        f.engineering = true;
        f.moving_gates = PE_GATES;
        f.still_gates = PE_GATES;
        f.moving[3] = 200;
        for (int i = 0; i < 20; i++) {
            b.now += 100;
            pe_step(&b.e, &f, b.now);
        }
        CHECK(b.e.frames_bad == 20 && b.e.presence == PE_PRESENCE_WAITING);
        pe_count_bad_frames(&b.e, 3);
        CHECK(b.e.frames_bad == 23);
    }

    test_begin("32-bit clock wrapping around during the countdown");
    {
        Bench b(0xFFFFFFFFu - 30000); // wraps around 30 s later, in the middle of the countdown
        b.learn_noise();
        b.person = { 3, 70, 40, 0 };
        b.run(3000);
        b.person = {};
        const uint32_t gone = b.now;
        b.run(120 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] - gone >= 60000 && b.absents[0] - gone <= 61500);
    }

    test_begin("Range: gates beyond it are not counted; a closer gate still triggers");
    {
        Bench b;
        b.range = { 75, 3, 3 };
        b.configure();
        CHECK(!pe_channel_counted(&b.e, PE_MOVING, 4) && pe_channel_counted(&b.e, PE_MOVING, 3));
        b.person = { 6, 90, 60, 0 };
        b.run(5000);
        CHECK(b.triggers.empty());
        b.person = { 2, 90, 60, 0 };
        b.run(2000);
        CHECK(b.triggers.size() == 1);
    }

    test_begin("radar threshold 100: the gate is ignored; still gates 0 and 1 never counted");
    {
        Bench b;
        b.radar.moving[5] = 100;
        b.radar.still[5] = 100;
        b.configure();
        b.person = { 5, 100, 100, 0 };
        b.run(5000);
        CHECK(b.triggers.empty());
        b.person = { 1, 0, 100, 0 }; // still energy at gate 1 only
        b.run(5000);
        CHECK(b.triggers.empty() && !pe_channel_counted(&b.e, PE_STILL, 1));
    }

    test_begin("noise recorded with 0.75 m gates, radar now at 0.2 m: hold at the radar threshold until back");
    {
        Bench b;
        b.learn_noise();
        CHECK(b.e.th.noise && pe_hold_threshold(&b.e, PE_STILL, 7) == 12);
        b.range.gate_cm = 20;
        b.configure();
        CHECK(!b.e.th.noise && pe_hold_threshold(&b.e, PE_STILL, 7) == 20);
        b.range.gate_cm = 75;
        b.configure();
        CHECK(b.e.th.noise && pe_hold_threshold(&b.e, PE_STILL, 7) == 12);
    }

    test_begin("new thresholds from the radar while present: presence stays, the countdown goes on");
    {
        Bench b;
        b.learn_noise();
        b.person = { 3, 70, 40, 0 };
        b.run(3000);
        b.person = {};
        b.run(10 * 1000);
        b.radar.moving[3] = 45; // learned again meanwhile
        b.configure();
        CHECK(b.e.presence == PE_PRESENCE_PRESENT && (b.events & PE_EV_THRESHOLDS));
        b.run(60 * 1000);
        CHECK(b.absents.size() == 1);
    }

    return test_end();
}
