// The ESP32 presence engine (presence_engine.cpp) alone, with simulated reports: per-channel thresholds from the
// learning statistics, the two-threshold presence with its countdown, sensor faults, the learning outcomes, the
// 32-bit clock wrapping around, the adaptation.
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

// Person: added on top of the room noise at a gate (moving and/or still energy)
struct Person {
    int gate = -1;
    int moving = 0;
    int still = 0;
};

static void add_person(pe_frame_t &f, const Person &p)
{
    if (p.gate < 0) {
        return;
    }
    f.moving[p.gate] = (uint8_t)std::max<int>(f.moving[p.gate], p.moving);
    f.still[p.gate] = (uint8_t)std::max<int>(f.still[p.gate], p.still);
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
    bool radar_on = true;   // reports arrive
    bool energies = true;   // with energies
    int saves = 0;
    bool save_ok = true;
    pe_profile_t saved = {};
    std::vector<uint32_t> triggers; // times presence started
    std::vector<uint32_t> absents;  // times presence ended
    uint32_t events = 0;

    static bool save_cb(const pe_profile_t *profile, void *ctx)
    {
        Bench *b = (Bench *)ctx;
        b->saves++;
        if (b->save_ok) {
            b->saved = *profile;
        }
        return b->save_ok;
    }

    explicit Bench(uint32_t start_ms = 1000, uint64_t seed = 1) : now(start_ms), rng(seed), room(typical_room())
    {
        pe_default_params(&p);
        pe_init(&e, &p, save_cb, this, now);
        const pe_range_t range = { 75, 8, 8 };
        pe_set_range(&e, &range, now);
    }
    ~Bench() { pe_deinit(&e); }

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
                add_person(f, person);
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

    // Learns the room as it is (no person): delay + recording, then a little more
    pe_learn_result_t learn()
    {
        CHECK(pe_learn_start(&e, now));
        run(p.learn_delay_ms + p.learn_duration_ms + 200);
        return e.learn_report.result;
    }
};

static int count_distinct_thresholds(const pe_profile_t &p, int kind)
{
    std::vector<int> values;
    for (int g = 0; g < PE_GATES; g++) {
        values.push_back(p.ch[kind][g].trigger);
    }
    std::sort(values.begin(), values.end());
    return (int)(std::unique(values.begin(), values.end()) - values.begin());
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("ESP32 presence engine:\n");

    test_begin("channel thresholds: from median, spread, 99th percentile and maximum; trigger above hold");
    {
        pe_params_t p;
        pe_default_params(&p);
        uint16_t hist[PE_MAX_ENERGY + 1] = {};
        pe_channel_t ch;
        // Silent gate: the floors
        hist[0] = 600;
        pe_compute_channel(&p, hist, 600, &ch);
        CHECK(ch.median == 0 && ch.max == 0 && ch.hold == p.hold_floor && ch.trigger == p.trigger_floor);
        CHECK(ch.trigger > ch.hold && ch.flags == 0);
        // 30 +- 5 with a rare 52
        memset(hist, 0, sizeof(hist));
        for (int v = 25; v <= 35; v++) {
            hist[v] = 50;
        }
        hist[45] = 5;
        hist[52] = 1;
        pe_compute_channel(&p, hist, 556, &ch);
        CHECK(ch.median == 30 && ch.max == 52);
        CHECK(ch.spread_x10 >= 40 && ch.spread_x10 <= 50); // MAD 3 -> 4.4
        CHECK(ch.hold >= ch.p99 + p.hold_margin && ch.hold >= 30 + 3 * 4);
        CHECK(ch.trigger >= ch.max + p.trigger_margin && ch.trigger >= ch.hold + p.trigger_gap);
        CHECK(ch.trigger <= 100 && ch.flags == 0);
        // Reaching 98 in the empty room: cannot trigger, can still hold
        hist[98] = 2;
        pe_compute_channel(&p, hist, 558, &ch);
        CHECK(ch.trigger == PE_OFF && ch.hold < 100 && (ch.flags & PE_CH_NOISY));
        // Always 95-100: not used at all
        memset(hist, 0, sizeof(hist));
        for (int v = 95; v <= 100; v++) {
            hist[v] = 100;
        }
        pe_compute_channel(&p, hist, 600, &ch);
        CHECK(ch.hold == PE_OFF && ch.trigger == PE_OFF && (ch.flags & PE_CH_NOISY));
        // No data
        memset(hist, 0, sizeof(hist));
        pe_compute_channel(&p, hist, 0, &ch);
        CHECK(ch.flags == PE_CH_NO_DATA && ch.hold == PE_OFF);
    }

    test_begin("boot: built-in defaults, waiting for a strong signal, sensor starting then OK");
    {
        Bench b;
        CHECK(!pe_profile_learned(&b.e) && b.e.presence == PE_PRESENCE_WAITING);
        CHECK(b.e.sensor == PE_SENSOR_STARTING);
        b.run(300);
        CHECK(b.e.sensor == PE_SENSOR_OK && !pe_occupied_report(&b.e));
        CHECK(b.e.active->ch[PE_MOVING][0].hold == 50 && b.e.active->ch[PE_MOVING][0].trigger == 65);
        CHECK(b.e.active->ch[PE_STILL][8].hold == 20 && b.e.active->ch[PE_STILL][8].trigger == 35);
    }

    test_begin("learning: waits, records, saves a per-gate profile (not one threshold for all gates)");
    {
        Bench b;
        CHECK(pe_learn_start(&b.e, b.now));
        CHECK(!pe_learn_start(&b.e, b.now)); // already running
        b.run(b.p.learn_delay_ms - 200);
        CHECK(b.e.learn_state == PE_LEARN_WAITING && b.e.hist == nullptr);
        b.run(400);
        CHECK(b.e.learn_state == PE_LEARN_RECORDING && b.e.hist != nullptr);
        CHECK(pe_learn_remaining_ms(&b.e, b.now) > 59000);
        b.run(b.p.learn_duration_ms);
        CHECK(b.e.learn_state == PE_LEARN_IDLE && b.e.hist == nullptr);
        CHECK(b.e.learn_report.result == PE_LEARN_OK && b.saves == 1);
        CHECK(pe_profile_valid(&b.saved) && b.saved.learned == 1 && b.saved.sequence == 1 && b.saved.gate_cm == 75);
        CHECK(b.saved.frames >= 590 && b.saved.frames <= 610 && b.saved.gates == PE_GATES);
        CHECK(pe_profile_learned(&b.e) && memcmp(b.e.active, &b.saved, sizeof(b.saved)) == 0);
        CHECK(count_distinct_thresholds(b.saved, PE_MOVING) >= 4 && count_distinct_thresholds(b.saved, PE_STILL) >= 4);
        for (int kind = 0; kind < PE_KINDS; kind++) {
            for (int g = 0; g < PE_GATES; g++) {
                const pe_channel_t &ch = b.saved.ch[kind][g];
                CHECK(ch.trigger < PE_OFF && ch.trigger > ch.hold);
                CHECK(fabs(ch.median - b.room.median[kind][g]) <= 1.0);
            }
        }
        // Near gates are noisier than far gates: higher thresholds there
        CHECK(b.saved.ch[PE_MOVING][0].trigger > b.saved.ch[PE_MOVING][7].trigger + 15);
        CHECK(b.events & PE_EV_LEARN_RESULT);
    }

    test_begin("empty room after learning: 30 min of noise and spikes, never present");
    {
        Bench b(1000, 7);
        CHECK(b.learn() == PE_LEARN_OK);
        b.room.spike_rate = 0.01; // more spikes than while learning
        b.run(30 * 60 * 1000);
        CHECK(b.triggers.empty() && b.e.presence == PE_PRESENCE_WAITING);
    }

    test_begin("single and double strong reports (noise bursts) never start presence");
    {
        Bench b(1000, 3);
        CHECK(b.learn() == PE_LEARN_OK);
        for (int i = 0; i < 50; i++) {
            const int gate = i % PE_GATES;
            b.person = { gate, 100, 0 };
            b.run(i % 2 ? 100 : 200); // 1 or 2 reports
            b.person = {};
            b.run(3000);
        }
        CHECK(b.triggers.empty());
        // Strong reports far apart (different gates in a row) do not add up either
        for (int i = 0; i < 20; i++) {
            b.person = { (i * 4) % PE_GATES, 100, 0 };
            b.run(100);
        }
        b.person = {};
        b.run(1000);
        CHECK(b.triggers.empty());
    }

    test_begin("someone walking in: present after 4 strong reports (about 0.4 s)");
    {
        Bench b(1000, 5);
        CHECK(b.learn() == PE_LEARN_OK);
        b.run(5000);
        const uint32_t start = b.now;
        b.person = { 3, 70, 30 };
        b.run(300);
        CHECK(b.triggers.empty());
        b.run(100);
        CHECK(b.triggers.size() == 1 && b.triggers[0] == start + 400);
        CHECK(pe_occupied_report(&b.e) && b.e.trigger_gate == 3 && b.e.trigger_kind == PE_MOVING);
        CHECK(b.e.trigger_energy == 70 && b.e.trigger_threshold == pe_trigger_threshold(&b.e, PE_MOVING, 3));
        // On the border of gates 3 and 4: the energy alternates between them, the neighbours count together
        Bench c(1000, 6);
        CHECK(c.learn() == PE_LEARN_OK);
        c.run(5000);
        for (int i = 0; i < 6; i++) {
            c.person = { 3 + i % 2, 60, 0 };
            c.run(100);
        }
        CHECK(c.triggers.size() == 1);
    }

    test_begin("a weak signal (above hold, below trigger) never starts presence from an empty room");
    {
        Bench b(1000, 8);
        CHECK(b.learn() == PE_LEARN_OK);
        const int gate = 6;
        const int weak = (pe_hold_threshold(&b.e, PE_STILL, gate) + pe_trigger_threshold(&b.e, PE_STILL, gate)) / 2;
        CHECK(weak >= pe_hold_threshold(&b.e, PE_STILL, gate) && weak < pe_trigger_threshold(&b.e, PE_STILL, gate));
        b.person = { gate, 0, weak };
        b.run(5 * 60 * 1000);
        CHECK(b.triggers.empty());
    }

    test_begin("someone sitting still far away keeps presence with the hold thresholds; empty 60 s after leaving");
    {
        Bench b(1000, 9);
        CHECK(b.learn() == PE_LEARN_OK);
        b.person = { 7, 60, 40 }; // walks in
        b.run(1000);
        CHECK(b.triggers.size() == 1);
        // Sits at gate 7: still energy just above the hold threshold, nothing near the trigger
        const int hold = pe_hold_threshold(&b.e, PE_STILL, 7);
        b.person = { 7, 0, hold + 2 };
        b.run(20 * 60 * 1000);
        CHECK(b.absents.empty() && pe_occupied_report(&b.e));
        // Leaves
        b.person = {};
        const uint32_t left = b.now;
        b.run(50 * 1000);
        CHECK(b.absents.empty() && !pe_occupied_report(&b.e)); // counting down: the cluster counts too
        const uint32_t remaining = pe_remaining_ms(&b.e, b.now);
        CHECK(remaining > 9000 && remaining <= 11000);
        b.run(15 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] >= left + 60 * 1000 && b.absents[0] <= left + 61 * 1000);
        CHECK(b.e.presence == PE_PRESENCE_WAITING);
    }

    test_begin("countdown starts again when the hold signal comes back");
    {
        Bench b(1000, 10);
        CHECK(b.learn() == PE_LEARN_OK);
        b.person = { 4, 70, 40 };
        b.run(1000);
        b.person = {};
        b.run(40 * 1000);
        CHECK(b.absents.empty());
        b.person = { 4, 0, pe_hold_threshold(&b.e, PE_STILL, 4) + 3 };
        b.run(1000);
        const uint32_t last = b.now;
        CHECK(pe_occupied_report(&b.e) && pe_remaining_ms(&b.e, b.now) == UINT32_MAX);
        b.person = {};
        b.run(55 * 1000);
        CHECK(b.absents.empty());
        b.run(10 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] >= last + 60 * 1000 && b.absents[0] <= last + 61 * 1000);
    }

    test_begin("hold time from Occupancy Sensing (30 s)");
    {
        Bench b(1000, 11);
        CHECK(b.learn() == PE_LEARN_OK);
        pe_set_absence_time(&b.e, 30 * 1000);
        b.person = { 2, 80, 30 };
        b.run(1000);
        b.person = {};
        const uint32_t left = b.now;
        b.run(35 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] >= left + 30 * 1000 && b.absents[0] <= left + 31 * 1000);
    }

    test_begin("empty room noise does not keep presence: 20 rooms, each empty again 60-62 s after the person left");
    {
        int late = 0;
        for (uint64_t seed = 20; seed < 40; seed++) {
            Bench b(1000, seed);
            CHECK(b.learn() == PE_LEARN_OK);
            b.person = { (int)(seed % 7) + 1, 80, 40 };
            b.run(2000);
            b.person = {};
            const uint32_t left = b.now;
            b.run(120 * 1000);
            CHECK(b.absents.size() == 1);
            if (b.absents.empty() || b.absents[0] > left + 62 * 1000) {
                late++;
            }
        }
        CHECK(late == 0);
    }

    test_begin("radar silent while present: stays present (no data is no empty room), full countdown once back");
    {
        Bench b(1000, 12);
        CHECK(b.learn() == PE_LEARN_OK);
        b.person = { 3, 70, 30 };
        b.run(1000);
        b.person = {};
        b.run(30 * 1000); // counting down
        b.radar_on = false;
        b.run(2500);
        CHECK(b.e.sensor == PE_SENSOR_NO_DATA && pe_sensor_fault(&b.e));
        CHECK(pe_occupied_report(&b.e) && pe_remaining_ms(&b.e, b.now) == UINT32_MAX);
        b.run(10 * 60 * 1000);
        CHECK(b.absents.empty() && b.e.presence == PE_PRESENCE_PRESENT);
        b.radar_on = true;
        b.run(200);
        CHECK(b.e.sensor == PE_SENSOR_OK);
        const uint32_t back = b.now;
        b.run(55 * 1000);
        CHECK(b.absents.empty());
        b.run(10 * 1000);
        CHECK(b.absents.size() == 1 && b.absents[0] >= back + 59 * 1000 && b.absents[0] <= back + 61 * 1000);
    }

    test_begin("radar silent while empty: fault reported, nothing invented; reports without energies: fault too");
    {
        Bench b(1000, 13);
        b.run(1000);
        b.radar_on = false;
        b.run(1900);
        CHECK(b.e.sensor == PE_SENSOR_OK);
        b.run(200);
        CHECK(b.e.sensor == PE_SENSOR_NO_DATA && !pe_occupied_report(&b.e));
        b.radar_on = true;
        b.energies = false;
        b.run(1000);
        CHECK(b.e.sensor == PE_SENSOR_NO_ENERGY && b.e.frames_no_energy == 10);
        pe_set_busy(&b.e, true, b.now);
        CHECK(b.e.sensor == PE_SENSOR_CONFIGURING);
        pe_set_busy(&b.e, false, b.now);
        b.energies = true;
        b.run(100);
        CHECK(b.e.sensor == PE_SENSOR_OK && (b.events & PE_EV_SENSOR));
        CHECK(b.triggers.empty());
    }

    test_begin("damaged reports (energy above 100) are ignored and counted, never taken as presence or absence");
    {
        Bench b(1000, 14);
        b.run(500);
        pe_frame_t bad = room_frame(b.room, b.rng);
        bad.moving[3] = 180;
        for (int i = 0; i < 30; i++) {
            b.now += 100;
            pe_step(&b.e, &bad, b.now);
            pe_tick(&b.e, b.now);
        }
        CHECK(b.e.frames_bad == 30 && b.e.sensor == PE_SENSOR_NO_DATA && b.triggers.empty());
        pe_count_bad_frames(&b.e, 5);
        CHECK(b.e.frames_bad == 35);
    }

    test_begin("32-bit clock wrapping around during the learning and the countdown");
    {
        Bench b(0xFFFFFFFFu - 50 * 1000, 15); // wraps 50 s from now, in the waiting time of the learning... no:
        // the learning (80 s) wraps; then the countdown wraps once more 2^32 ms later is not testable, so start the
        // countdown just before the wrap with a second bench below.
        CHECK(b.learn() == PE_LEARN_OK && b.now < 0x10000000u);
        Bench c(0xFFFFFFFFu - 130 * 1000, 16);
        CHECK(c.learn() == PE_LEARN_OK);
        c.person = { 5, 60, 30 };
        c.run(1000);
        CHECK(c.triggers.size() == 1);
        c.person = {};
        const uint32_t left = c.now; // about 49 s before the wrap
        c.run(70 * 1000);
        CHECK(c.absents.size() == 1 && c.absents[0] - left >= 60 * 1000 && c.absents[0] - left <= 61 * 1000);
        CHECK(c.now < left); // wrapped
    }

    test_begin("Range: gates beyond it are not counted; a closer gate still triggers");
    {
        Bench b(1000, 17);
        CHECK(b.learn() == PE_LEARN_OK);
        const pe_range_t range = { 75, 5, 5 };
        pe_set_range(&b.e, &range, b.now);
        CHECK(!pe_channel_counted(&b.e, PE_MOVING, 7) && pe_channel_counted(&b.e, PE_MOVING, 5));
        b.person = { 7, 100, 100 };
        b.run(5000);
        CHECK(b.triggers.empty());
        b.person = { 4, 80, 0 };
        b.run(500);
        CHECK(b.triggers.size() == 1);
    }

    test_begin("profile learned for 0.75 m gates: built-in defaults while the radar uses 0.2 m gates");
    {
        Bench b(1000, 18);
        CHECK(b.learn() == PE_LEARN_OK && pe_profile_learned(&b.e));
        pe_range_t fine = { 20, 8, 8 };
        pe_set_range(&b.e, &fine, b.now);
        CHECK(!pe_profile_learned(&b.e) && (b.e.active->ch[PE_MOVING][3].flags & PE_CH_DEFAULT));
        pe_range_t coarse = { 75, 8, 8 };
        pe_set_range(&b.e, &coarse, b.now);
        CHECK(pe_profile_learned(&b.e));
    }

    test_begin("stored profile: accepted when valid; damaged, unlearned or another version refused");
    {
        Bench a(1000, 19);
        CHECK(a.learn() == PE_LEARN_OK);
        Bench b(1000, 20);
        CHECK(pe_set_profile(&b.e, &a.saved, b.now) && pe_profile_learned(&b.e));
        pe_profile_t bad = a.saved;
        bad.ch[PE_STILL][4].hold ^= 1;
        CHECK(!pe_set_profile(&b.e, &bad, b.now)); // CRC
        bad = a.saved;
        bad.version = 2;
        bad.crc32 = pe_profile_crc(&bad);
        CHECK(!pe_set_profile(&b.e, &bad, b.now));
        bad = a.saved;
        bad.ch[PE_MOVING][2].trigger = bad.ch[PE_MOVING][2].hold; // trigger must be above hold
        bad.crc32 = pe_profile_crc(&bad);
        CHECK(!pe_set_profile(&b.e, &bad, b.now));
        pe_profile_t defaults;
        pe_default_profile(&defaults);
        CHECK(pe_profile_valid(&defaults) && !pe_set_profile(&b.e, &defaults, b.now));
        CHECK(pe_profile_learned(&b.e)); // still the good one
        pe_reset_profile(&b.e, b.now);
        CHECK(!pe_profile_learned(&b.e));
    }

    test_begin("a failed learning keeps the old profile: too noisy (someone moving while recording)");
    {
        Bench b(1000, 21);
        CHECK(b.learn() == PE_LEARN_OK && b.saves == 1);
        const pe_profile_t first = b.saved;
        // Someone walks around every gate during the recording
        CHECK(pe_learn_start(&b.e, b.now));
        b.run(b.p.learn_delay_ms + 200);
        for (uint32_t t = 0; t < b.p.learn_duration_ms; t += 2000) {
            b.person = { (int)((t / 2000) % PE_GATES), 99, 99 };
            b.run(2000);
        }
        b.person = {};
        b.run(500);
        CHECK(b.e.learn_report.result == PE_LEARN_TOO_NOISY || b.e.learn_report.result == PE_LEARN_NO_USABLE);
        CHECK(b.saves == 1 && memcmp(&b.e.stored, &first, sizeof(first)) == 0 && pe_profile_learned(&b.e));
        CHECK(b.e.has_candidate && b.e.learn_report.noisy > b.e.learn_report.channels / 2);
    }

    test_begin("learning failures: radar silent, too few reports, settings changed, cancel, save failed");
    {
        Bench b(1000, 22);
        CHECK(pe_learn_start(&b.e, b.now));
        b.run(b.p.learn_delay_ms + 200);
        b.radar_on = false;
        b.run(2000);
        CHECK(b.e.learn_report.result == PE_LEARN_DATA_GAP && b.e.learn_state == PE_LEARN_IDLE);
        CHECK(b.e.hist == nullptr && b.saves == 0);

        // About 3 reports per second (radar overloaded, noisy line): too few
        Bench c(1000, 23);
        CHECK(pe_learn_start(&c.e, c.now));
        c.run(c.p.learn_delay_ms + 100);
        for (uint32_t t = 0; t < c.p.learn_duration_ms + 1000; t += 300) {
            c.now += 300;
            const pe_frame_t f = room_frame(c.room, c.rng);
            pe_step(&c.e, &f, c.now);
            pe_tick(&c.e, c.now);
        }
        CHECK(c.e.learn_report.result == PE_LEARN_NO_DATA && c.saves == 0);

        Bench d(1000, 24);
        CHECK(pe_learn_start(&d.e, d.now));
        d.run(d.p.learn_delay_ms + 5000);
        const pe_range_t fine = { 20, 8, 8 };
        pe_set_range(&d.e, &fine, d.now);
        CHECK(d.e.learn_report.result == PE_LEARN_SETTINGS_CHANGED && d.e.hist == nullptr);

        Bench e(1000, 25);
        CHECK(pe_learn_start(&e.e, e.now));
        e.run(5000);
        CHECK(pe_learn_cancel(&e.e, e.now) && e.e.learn_report.result == PE_LEARN_CANCELLED);
        CHECK(!pe_learn_cancel(&e.e, e.now));
        CHECK(pe_learn_start(&e.e, e.now));
        e.run(e.p.learn_delay_ms + 3000);
        CHECK(e.e.hist != nullptr && pe_learn_cancel(&e.e, e.now) && e.e.hist == nullptr);

        Bench f(1000, 26);
        f.save_ok = false;
        CHECK(f.learn() == PE_LEARN_SAVE_FAILED && f.saves == 1 && !pe_profile_learned(&f.e));
    }

    test_begin("adaptation: only after 5 min empty and quiet, bounded, frozen while present, never stored");
    {
        Bench b(1000, 27);
        CHECK(b.learn() == PE_LEARN_OK);
        const int g = 5;
        const uint8_t hold = pe_hold_threshold(&b.e, PE_MOVING, g);
        const uint8_t trigger = pe_trigger_threshold(&b.e, PE_MOVING, g);
        // The noise floor of gate 5 rises by 4 (stays below the trigger)
        b.room.median[PE_MOVING][g] += 4;
        b.run(4 * 60 * 1000);
        CHECK(b.e.offset[PE_MOVING][g] == 0 && !b.e.quiet); // not quiet long enough yet
        b.run(20 * 60 * 1000);
        CHECK(b.e.quiet && b.e.offset[PE_MOVING][g] >= 3 && b.e.offset[PE_MOVING][g] <= 5);
        CHECK(pe_hold_threshold(&b.e, PE_MOVING, g) == hold + b.e.offset[PE_MOVING][g]);
        CHECK(pe_trigger_threshold(&b.e, PE_MOVING, g) == trigger + b.e.offset[PE_MOVING][g]);
        CHECK(b.e.offset[PE_MOVING][1] <= 1); // the others stay
        CHECK(b.saves == 1);                  // nothing stored
        CHECK(b.triggers.empty());
        // Present: frozen
        b.person = { 2, 90, 40 };
        b.run(1000);
        const uint8_t frozen = b.e.offset[PE_MOVING][g];
        b.room.median[PE_MOVING][g] -= 4;
        b.run(10 * 60 * 1000);
        CHECK(b.e.presence == PE_PRESENCE_PRESENT && b.e.offset[PE_MOVING][g] == frozen);
        // Gone, then quiet again: comes back down, never below the learned thresholds
        b.person = {};
        b.run(30 * 60 * 1000);
        CHECK(b.e.offset[PE_MOVING][g] <= 1 && pe_hold_threshold(&b.e, PE_MOVING, g) >= hold);
    }

    test_begin("adaptation: at most adapt_max_up");
    {
        Bench b(1000, 29);
        b.p.adapt_max_up = 2;
        pe_init(&b.e, &b.p, Bench::save_cb, &b, b.now);
        const pe_range_t range = { 75, 8, 8 };
        pe_set_range(&b.e, &range, b.now);
        CHECK(b.learn() == PE_LEARN_OK);
        b.room.median[PE_STILL][6] += 4;
        b.run(30 * 60 * 1000);
        CHECK(b.e.offset[PE_STILL][6] == 2);
    }

    test_begin("adaptation off: thresholds never move");
    {
        Bench b(1000, 28);
        b.p.adapt = false;
        pe_init(&b.e, &b.p, Bench::save_cb, &b, b.now);
        CHECK(b.learn() == PE_LEARN_OK);
        b.room.median[PE_STILL][6] += 4;
        b.run(30 * 60 * 1000);
        CHECK(b.e.offset[PE_STILL][6] == 0);
    }

    return test_end();
}
