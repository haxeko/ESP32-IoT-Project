// The ESP32 presence engine (presence_engine.cpp) alone, with simulated reports: per-channel thresholds from the
// radar's (strong above them, hold below them but above the noise recorded while learning), the two-threshold
// presence with its countdown, sensor faults, the noise record, the 32-bit clock wrapping around.
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
};

// Hold threshold a channel should get: T - min(10, 40 % of T), at least noise + 2, at most T, at least 1
static int expected_hold(int t, int level)
{
    const int drop = std::min(10, (t * 40 + 50) / 100);
    int hold = std::max(t - drop, level + 2);
    return std::max(1, std::min(hold, t));
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

    test_begin("hold thresholds with the noise: radar - 40 % (at most 10), above the noise + 2, at most the radar");
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
        const int want[PE_GATES] = { 40, 47, 12, 9, 6, 3, 50, 89, 30 };
        for (int g = 0; g < PE_GATES; g++) {
            const pe_channel_t &ch = th.ch[PE_MOVING][g];
            CHECK(ch.hold == want[g] && ch.trigger == t[g] + 1);
            if (level[g] != PE_NOISE_UNKNOWN) {
                CHECK(ch.hold == expected_hold(t[g], level[g]));
            }
        }
        CHECK(th.ch[PE_MOVING][0].flags == 0 && th.ch[PE_MOVING][1].flags == PE_CH_NOISE_HOLD);
        CHECK(th.ch[PE_MOVING][6].flags == PE_CH_NOISE_HOLD && th.ch[PE_MOVING][8].flags == PE_CH_NO_NOISE);
        // Other settings: no drop at all, or a larger one
        p.hold_drop_percent = 0;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.ch[PE_MOVING][2].hold == 20);
        p.hold_drop_percent = 60;
        p.hold_drop_max = 15;
        pe_compute_thresholds(&p, &radar, &noise, &th);
        CHECK(th.ch[PE_MOVING][0].hold == 35 && th.ch[PE_MOVING][2].hold == 8);
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

    test_begin("someone walking in: present after 4 strong reports (about 0.4 s), the log names the gate");
    {
        Bench b;
        b.learn_noise();
        b.run(5000);
        const uint32_t in = b.now;
        b.person = { 4, 70, 30, 0 };
        b.run(1000);
        CHECK(b.triggers.size() == 1 && b.triggers[0] - in <= 500 && b.triggers[0] - in >= 300);
        CHECK(b.e.trigger_gate == 4 && b.e.trigger_kind == PE_MOVING && b.e.trigger_threshold == 20);
        CHECK(pe_occupied_report(&b.e));
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
        Bench b(0xFFFF0000u);
        b.learn_noise();
        b.person = { 3, 70, 40, 0 };
        b.run(3000);
        b.person = {};
        const uint32_t gone = b.now;
        b.run(120 * 1000); // wraps around after 65 s
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
