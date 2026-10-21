// Empty room learning and factory thresholds (main/quiet_learn.cpp) against the simulated radar. Built three times:
// with CONFIG_LD2410_LEARN_RADAR_AUTO=0 (learning on the ESP32, factory thresholds, threshold computation), =1 (the
// radar's own background noise detection, falling back to the ESP32), and with CONFIG_LD2410_LEARN_PEAK=1 (the
// ESP32 learns from the highest energy and the average, 120 s).
#include "sim.h"

#include <string.h>

#include "quiet_learn.h"
#include "radar_sim.h"

static const uint8_t kFactoryMoving[9] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kFactoryStill[9] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };

static std::vector<std::pair<int64_t, quiet_learn_phase_t>> s_phases;
static int64_t s_cancel_at_ms = -1;

static bool cancelled()
{
    return s_cancel_at_ms >= 0 && sim_ms() >= s_cancel_at_ms;
}

static void on_phase(quiet_learn_phase_t phase)
{
    s_phases.push_back({ sim_ms(), phase });
}

static void start(RadarSim &radar)
{
    sim_reset();
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.attach();
    s_phases.clear();
    s_cancel_at_ms = -1;
}

static int64_t phase_ms(quiet_learn_phase_t phase)
{
    for (const auto &p : s_phases) {
        if (p.second == phase) {
            return p.first;
        }
    }
    return -1;
}

static bool factory_thresholds(const RadarSim &radar)
{
    return memcmp(radar.moving, kFactoryMoving, 9) == 0 && memcmp(radar.still + 2, kFactoryStill + 2, 7) == 0;
}

#if !CONFIG_LD2410_LEARN_RADAR_AUTO

static void test_computation()
{
    test_begin("noise: energy that the percentile of the frames stays at or below, 100 = highest");
    static ld2410_capture_t c;
    memset(&c, 0, sizeof(c));
    for (int e = 0; e < 100; e++) {
        c.moving[0][e] = 1; // 100 frames, energies 0..99
    }
    c.moving[1][10] = 600; // 600 frames at 10, 20 at 60 (a burst)
    c.moving[1][60] = 20;
    c.still[2][0] = 0;     // no frames: 0
    quiet_learn_levels_t noise;
    quiet_learn_noise(&c, 95, &noise);
    CHECK(noise.moving[0] == 94 && noise.moving[1] == 10 && noise.still[2] == 0);
    quiet_learn_noise(&c, 100, &noise);
    CHECK(noise.moving[0] == 99 && noise.moving[1] == 60);
    quiet_learn_noise(&c, 50, &noise);
    CHECK(noise.moving[0] == 49);
    // Rank rounded up: 10 frames at 95 % is rank ceil(9.5) = 10, the highest of energies 0..9
    memset(&c, 0, sizeof(c));
    for (int e = 0; e < 10; e++) {
        c.still[4][e] = 1;
    }
    quiet_learn_noise(&c, 95, &noise);
    CHECK(noise.still[4] == 9);

    test_begin("thresholds: noise + margin, from the factory default to factory + raise, at most 100");
    quiet_learn_levels_t n = {};
    ld2410_thresholds_t current = {};
    current.still[0] = 77;
    current.still[1] = 66;
    n.moving[2] = 5;   // below the factory default: factory 40
    n.moving[3] = 35;  // 45, inside the limits
    n.moving[4] = 90;  // limited to 20 + 30
    n.still[0] = 90;   // gates 0 and 1: still threshold kept
    n.still[5] = 25;   // 35, above the factory 30
    ld2410_thresholds_t out;
    quiet_learn_compute_thresholds(&n, &current, 10, 30, &out);
    CHECK(out.moving[2] == 40 && out.moving[3] == 45 && out.moving[4] == 50);
    CHECK(out.still[0] == 77 && out.still[1] == 66 && out.still[5] == 35 && out.still[8] == 20);
    n.moving[4] = 100;
    quiet_learn_compute_thresholds(&n, &current, 10, 100, &out);
    CHECK(out.moving[4] == 100);
}

#if !CONFIG_LD2410_LEARN_PEAK
// Every gate 5 apart from the factory defaults, gate 3 still energy 75 (a fan), gate 5 moving 40 for 2 s (someone
// passing by)
static void room_energies(int64_t ms, uint8_t *moving, uint8_t *still)
{
    for (int g = 0; g < 9; g++) {
        moving[g] = 5;
        still[g] = 5;
    }
    still[3] = 75;
    moving[5] = (ms >= 40000 && ms < 42000) ? 40 : 15;
}

static void test_esp32_learning()
{
    test_begin("ESP32 learning: 95 % noise, margin, limits, written and read back");
    {
        RadarSim radar;
        start(radar);
        radar.energies = room_energies;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(phase_ms(QUIET_LEARN_WAITING) == 0);
        const int64_t rec = phase_ms(QUIET_LEARN_RECORDING);
        CHECK(rec >= 20000 && rec < 20100);
        CHECK(radar.still[3] == 70);                              // 75 + 10 limited to 40 + 30
        CHECK(radar.moving[5] == 25);                             // the burst is left out (peak 40)
        CHECK(radar.moving[0] == 50 && radar.moving[8] == 15);   // factory defaults
        CHECK(radar.still[2] == 40 && radar.still[8] == 20);
        CHECK(radar.count(0x0064) == 9 && !radar.engineering && !radar.config_mode);
        CHECK(sim_log_contains("Empty room learning: leave the room, recording starts in 20 s and lasts 60 s"));
        CHECK(sim_log_contains("Recording the empty room for 60 s"));
        CHECK(sim_log_contains("reports. Noise = energy of 95 % of them, threshold = noise + 10, from the factory "
                               "default to factory + 30"));
        CHECK(sim_log_contains("Gate 3:   5 (  5) ->  30 ( 30) |  75 ( 75) ->  70 ( 40)"));
        CHECK(sim_log_contains("Gate 5:  15 ( 40) ->  25 ( 15) |   5 (  5) ->  30 ( 30)"));
        CHECK(sim_log_contains("Gate 3 (2.25 m to 3 m): still noise 75, threshold limited to 70 (factory 40 + 30)"));
        CHECK(sim_log_contains("Learning done: new thresholds saved in the LD2410 ("));
    }

    test_begin("ESP32 learning: cancelled while waiting, nothing sent");
    {
        RadarSim radar;
        start(radar);
        s_cancel_at_ms = 5000;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_CANCELLED);
        CHECK(sim_ms() <= 5100 && radar.commands.empty());
    }

    test_begin("ESP32 learning: cancelled while recording, nothing written, engineering off");
    {
        RadarSim radar;
        start(radar);
        s_cancel_at_ms = 30000;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_CANCELLED);
        CHECK(radar.command_list() == "00FF 0062 00FE 00FF 0063 00FE");
        CHECK(factory_thresholds(radar) && !radar.engineering);
    }

    test_begin("ESP32 learning: too few reports, nothing written");
    {
        RadarSim radar;
        start(radar);
        sim_at(20500, [&radar]() { radar.report_silent = true; });
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("Only 4 engineering reports in 60 s, nothing changed") ||
              sim_log_contains("Only 5 engineering reports in 60 s, nothing changed"));
        CHECK(radar.count(0x0064) == 0 && !radar.engineering);
    }

    test_begin("ESP32 learning: thresholds not kept by the radar");
    {
        RadarSim radar;
        start(radar);
        radar.energies = room_energies;
        radar.stuck_gate = 3;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("Gate 3 reads back 30/40 instead of 30/70, the radar may hold a mix of old and new"));
    }

    test_begin("ESP32 learning: the radar does not answer");
    {
        RadarSim radar;
        start(radar);
        radar.tx_wire_broken = true;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("check the wire from GPIO4 to the LD2410 RX pin"));
    }

    test_begin("ESP32 learning: the radar stops answering while recording");
    {
        RadarSim radar;
        start(radar);
        sim_at(50000, [&radar]() { radar.tx_wire_broken = true; });
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("The radar stopped answering commands, nothing changed"));
        CHECK(factory_thresholds(radar));
    }
}
#endif

#if CONFIG_LD2410_LEARN_PEAK
static quiet_learn_room_t room_all(uint8_t avg, uint8_t peak, uint8_t noise)
{
    quiet_learn_room_t r = {};
    r.frames = 1200;
    for (int g = 0; g < 9; g++) {
        r.avg.moving[g] = r.avg.still[g] = avg;
        r.peak.moving[g] = r.peak.still[g] = peak;
        r.noise.moving[g] = r.noise.still[g] = noise;
        r.moving_valid[g] = r.still_valid[g] = true;
    }
    return r;
}

static ld2410_thresholds_t factory()
{
    ld2410_thresholds_t t;
    memcpy(t.moving, kFactoryMoving, 9);
    memcpy(t.still, kFactoryStill, 9);
    return t;
}

static void test_peak_computation()
{
    test_begin("peak: the numbers of the HLKRadarTool screenshots (average / highest): highest + the larger of 8 and "
               "(highest - average), at least 75 % of the factory thresholds");
    {
        const uint8_t moving_peak[9] = { 26, 17, 8, 9, 6, 7, 4, 7, 5 };
        const uint8_t moving_avg[9] = { 13, 7, 2, 4, 2, 4, 2, 3, 3 };
        const uint8_t still_peak[9] = { 0, 0, 12, 12, 6, 4, 3, 3, 3 };
        const uint8_t still_avg[9] = { 0, 0, 3, 4, 3, 2, 2, 2, 2 };
        quiet_learn_room_t room = room_all(0, 0, 0);
        memcpy(room.peak.moving, moving_peak, 9);
        memcpy(room.avg.moving, moving_avg, 9);
        memcpy(room.peak.still, still_peak, 9);
        memcpy(room.avg.still, still_avg, 9);
        ld2410_thresholds_t current = factory();
        current.still[0] = 21; // what the app shows there: not settable, kept
        current.still[1] = 24;
        const quiet_learn_peak_params_t params = { 75, 8, 30 };
        ld2410_thresholds_t out;
        quiet_learn_levels_t flags;
        quiet_learn_peak_thresholds(&room, &current, &params, &out, &flags);
        const uint8_t want_moving[9] = { 39, 38, 30, 23, 15, 15, 12, 15, 13 };
        const uint8_t want_still[9] = { 21, 24, 30, 30, 23, 23, 15, 15, 15 };
        CHECK(memcmp(out.moving, want_moving, 9) == 0 && memcmp(out.still, want_still, 9) == 0);
        CHECK(flags.moving[0] == 0 && flags.moving[1] == QUIET_LEARN_FLOOR && flags.moving[6] == 0);
        CHECK(flags.still[0] == QUIET_LEARN_KEPT && flags.still[2] == QUIET_LEARN_FLOOR);
        // Floor 100 %: never below the factory thresholds (here: the factory thresholds themselves)
        const quiet_learn_peak_params_t never_lower = { 100, 8, 30 };
        quiet_learn_peak_thresholds(&room, &current, &never_lower, &out, nullptr);
        CHECK(memcmp(out.moving, kFactoryMoving, 9) == 0 && out.still[2] == 40 && out.still[8] == 20);
        // Floor 0: the room alone
        const quiet_learn_peak_params_t room_only = { 0, 8, 30 };
        quiet_learn_peak_thresholds(&room, &current, &room_only, &out, nullptr);
        CHECK(out.moving[1] == 27 && out.moving[2] == 16 && out.still[2] == 21 && out.still[8] == 11);
    }

    test_begin("peak: at least the noise + 4, at most factory + 30 (and 99), kept where nothing was recorded; floor "
               "and ceiling from the factory thresholds, learning again gives the same thresholds");
    {
        quiet_learn_room_t room = room_all(3, 6, 0);
        room.peak.still[2] = 85;        // something moved there: 85 + 82 limited to 40 + 30
        room.moving_valid[7] = false;   // no reports: kept
        room.noise.still[4] = 30;       // the robust noise above highest + 8: 30 + 4
        ld2410_thresholds_t current = factory();
        current.moving[7] = 44;
        const quiet_learn_peak_params_t params = { 75, 8, 30 };
        ld2410_thresholds_t out;
        quiet_learn_levels_t flags;
        quiet_learn_peak_thresholds(&room, &current, &params, &out, &flags);
        CHECK(out.still[2] == 70 && flags.still[2] == QUIET_LEARN_LIMIT);
        CHECK(out.still[4] == 34 && flags.still[4] == QUIET_LEARN_NOISE);
        CHECK(out.moving[7] == 44 && flags.moving[7] == QUIET_LEARN_KEPT);
        CHECK(out.moving[8] == 14 && flags.moving[8] == 0); // 6 + 8, floor 12
        CHECK(out.moving[0] == 38 && flags.moving[0] == QUIET_LEARN_FLOOR);
        const quiet_learn_peak_params_t no_limit = { 75, 8, 100 }; // at most 99, never 100 (the gate is ignored)
        quiet_learn_peak_thresholds(&room, &current, &no_limit, &out, &flags);
        CHECK(out.still[2] == 99 && flags.still[2] == QUIET_LEARN_LIMIT);
        ld2410_thresholds_t again;
        quiet_learn_peak_thresholds(&room, &current, &params, &out, nullptr);
        quiet_learn_peak_thresholds(&room, &out, &params, &again, nullptr);
        CHECK(memcmp(&out, &again, sizeof(out)) == 0);
        ld2410_thresholds_t other = current; // thresholds set with HLKRadarTool: floor and ceiling stay factory ones
        other.moving[3] = 60;                // factory 30: floor 23
        other.moving[5] = 5;                 // factory 15: ceiling 45
        room.peak.moving[5] = 60;
        quiet_learn_peak_thresholds(&room, &other, &params, &out, &flags);
        CHECK(out.moving[3] == 23 && flags.moving[3] == QUIET_LEARN_FLOOR);
        CHECK(out.moving[5] == 45 && flags.moving[5] == QUIET_LEARN_LIMIT);
    }

    test_begin("peak: room statistics: rounded average, highest report, noise (99th percentile, at least median + 3 "
               "robust spreads), gates without reports");
    {
        static ld2410_capture_t c;
        memset(&c, 0, sizeof(c));
        c.frames = 300;
        c.moving[0][10] = 9; // 9 x 10 and one 60: average 15, highest 60
        c.moving[0][60] = 1;
        c.moving[1][10] = 300; // constant 10: median 10, spread at least 1 -> noise 13
        c.moving[2][10] = 100; // 10, 12, 14: median 12, MAD 2, spread 2.966 -> 21 (99th percentile 14)
        c.moving[2][12] = 100;
        c.moving[2][14] = 100;
        c.moving[4][5] = 290; // 290 at 5 and 10 at 40: 99th percentile 40
        c.moving[4][40] = 10;
        c.still[3][4] = 1; // 4 and 5: average 4.5 -> 5
        c.still[3][5] = 1;
        // moving gate 3: nothing recorded
        quiet_learn_room_t room;
        quiet_learn_room(&c, &room);
        CHECK(room.frames == 300 && room.moving_valid[0] && !room.moving_valid[3] && !room.still_valid[0]);
        CHECK(room.avg.moving[0] == 15 && room.peak.moving[0] == 60 && room.noise.moving[0] == 60);
        CHECK(room.noise.moving[1] == 13 && room.peak.moving[1] == 10 && room.noise.moving[2] == 21);
        CHECK(room.noise.moving[4] == 40 && room.peak.moving[4] == 40 && room.avg.moving[4] == 6);
        CHECK(room.avg.still[3] == 5 && room.peak.still[3] == 5);
    }
}

// Every gate 3 (quiet), moving gate 3 steady at 18, still gate 3 at 75 (a fan), moving gate 6 a lone report at 15
// every 5 s (the radar does not filter it as a median of three would)
static void peak_room(int64_t ms, uint8_t *moving, uint8_t *still)
{
    for (int g = 0; g < 9; g++) {
        moving[g] = 3;
        still[g] = 3;
    }
    moving[3] = 18;
    still[3] = 75;
    moving[6] = ms % 5000 < 100 ? 15 : 2;
}

static void test_peak_learning()
{
    test_begin("peak: recorded by the ESP32 for 120 s after the delay, never the radar's own detection; thresholds "
               "written once and read back; a lone spike counts");
    {
        RadarSim radar;
        start(radar);
        radar.energies = peak_room;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(phase_ms(QUIET_LEARN_WAITING) == 0);
        const int64_t rec = phase_ms(QUIET_LEARN_RECORDING);
        CHECK(rec >= 20000 && rec < 20100);
        CHECK(radar.count(0x000B) == 0 && radar.count(0x0064) == 9 && !radar.engineering && !radar.config_mode);
        CHECK(radar.moving[6] == 28);                         // 15 + (15 - 2), above the lone spikes
        CHECK(radar.moving[3] == 26);                         // 18 + 8, above the floor 23
        CHECK(radar.still[3] == 70);                          // 75 + 8 limited to 40 + 30
        CHECK(radar.moving[0] == 38 && radar.moving[8] == 12); // floor: 75 % of 50 and of 15
        CHECK(radar.still[2] == 30 && radar.still[8] == 15);
        CHECK(sim_log_contains("Empty room learning: leave the room, recording starts in 20 s and lasts 120 s"));
        CHECK(sim_log_contains("reports. Threshold = highest + the larger of 8 and (highest - average), at least the "
                               "noise + 4, from 75 % of the factory default to factory + 30"));
        CHECK(sim_log_contains("Gate 6 (4.5 m to 5.25 m): moving avg 2 peak 15 noise 15 -> 28 (was 15) | still avg 3 "
                               "peak 3 noise 6 -> 15 floor (was 20)"));
        CHECK(sim_log_contains("Gate 0 (0 m to 0.75 m): moving avg 3 peak 3 noise 6 -> 38 floor (was 50) | still not "
                               "settable"));
        CHECK(sim_log_contains("Gate 3 (2.25 m to 3 m): still highest 75, average 75: threshold limited to 70 "
                               "(factory 40 + 30). Something kept moving there"));
        CHECK(sim_log_contains("Learning done: new thresholds saved in the LD2410 ("));

        test_begin("peak: the same room again: the radar holds these thresholds already, nothing written");
        radar.commands.clear();
        s_phases.clear();
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(radar.count(0x0064) == 0 && radar.moving[6] == 28);
        CHECK(sim_log_contains("The radar holds these thresholds already, nothing written"));
    }

    test_begin("peak: fewer than 5 reports per second, failed, nothing written");
    {
        RadarSim radar;
        start(radar);
        radar.energies = peak_room;
        sim_at(75000, [&radar]() { radar.report_silent = true; }); // 55 s of the 120 s: about 550 reports
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("engineering reports in 120 s, nothing changed"));
        CHECK(radar.count(0x0064) == 0 && factory_thresholds(radar) && !radar.engineering);
    }

    test_begin("peak: cancelled while waiting or recording, nothing written");
    {
        RadarSim radar;
        start(radar);
        s_cancel_at_ms = 5000;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_CANCELLED);
        CHECK(radar.commands.empty());
        RadarSim again;
        start(again);
        s_cancel_at_ms = 60000;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_CANCELLED);
        CHECK(again.count(0x0064) == 0 && factory_thresholds(again) && !again.engineering);
    }

    test_begin("peak: the radar does not keep the thresholds, failed");
    {
        RadarSim radar;
        start(radar);
        radar.energies = peak_room;
        radar.stuck_gate = 3;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("Gate 3 reads back 30/40 instead of 26/70, the radar may hold a mix of old and new"));
    }

    test_begin("peak: the radar does not answer, failed");
    {
        RadarSim radar;
        start(radar);
        radar.tx_wire_broken = true;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("check the wire from GPIO4 to the LD2410 RX pin"));
    }
}
#endif

static void test_factory_thresholds()
{
    test_begin("factory thresholds: written and read back, still thresholds of gates 0-1 kept");
    {
        RadarSim radar;
        start(radar);
        radar.moving[3] = 60;
        radar.still[3] = 70;
        radar.still[0] = 33;
        radar.max_moving_gate = 5;
        radar.no_one_duration = 60;
        CHECK(quiet_learn_restore_factory());
        CHECK(factory_thresholds(radar) && radar.still[0] == 33);
        CHECK(radar.max_moving_gate == 5 && radar.no_one_duration == 60 && radar.restarts == 0);
        CHECK(radar.count(0x0064) == 9 && !radar.config_mode);
        CHECK(sim_log_contains("Restoring the LD2410 factory thresholds"));
        CHECK(sim_log_contains("Thresholds until now: moving 50,50,40,60,20,15,15,15,15 | still -,-,40,70,30,30,20,20,20"));
        CHECK(sim_log_contains("Factory thresholds: moving 50,50,40,30,20,15,15,15,15 | still -,-,40,40,30,30,20,20,20"));
    }

    test_begin("factory thresholds: already held, nothing written");
    {
        RadarSim radar;
        start(radar);
        CHECK(quiet_learn_restore_factory());
        CHECK(radar.count(0x0064) == 0 && sim_log_contains("holds the factory thresholds already"));
    }

    test_begin("factory thresholds: not kept, or no answer");
    {
        RadarSim radar;
        start(radar);
        radar.moving[4] = 70;
        radar.stuck_gate = 4;
        CHECK(!quiet_learn_restore_factory());
        CHECK(!radar.config_mode);
    }
    {
        RadarSim radar;
        start(radar);
        radar.tx_wire_broken = true;
        CHECK(!quiet_learn_restore_factory());
    }
}

#else // CONFIG_LD2410_LEARN_RADAR_AUTO

static void test_radar_auto()
{
    test_begin("radar learning: STATE 4 then 5, thresholds set by the radar, nothing written by the ESP32");
    {
        RadarSim radar;
        start(radar);
        radar.auto_moving[2] = 80; // more than factory + 30: warned
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(radar.count(0x000B) == 1 && radar.auto_seconds == 60 && radar.count(0x0064) == 0);
        CHECK(radar.moving[2] == 80 && radar.still[3] == 30 && !radar.engineering);
        const int64_t rec = phase_ms(QUIET_LEARN_RECORDING);
        CHECK(phase_ms(QUIET_LEARN_WAITING) == 0 && rec >= 20000 && rec < 20200);
        CHECK(sim_ms() >= 80000 && sim_ms() < 81000);
        CHECK(sim_log_contains("LD2410 firmware V2.44.25070917"));
        CHECK(sim_log_contains("The LD2410 measures the empty room itself: 10 s more to leave, then 60 s"));
        CHECK(sim_log_contains("Thresholds before: moving 50,50,40,30,20,15,15,15,15 | still -,-,40,40,30,30,20,20,20"));
        CHECK(sim_log_contains("Thresholds set by the radar: moving 45,40,80,25,18,12,10,10,10 | still -,-,35,30,25,22,15,15,15"));
        CHECK(sim_log_contains("Gate 2: moving threshold 80, more than factory 40 + 30"));
        CHECK(sim_log_contains("Learning done: thresholds set by the LD2410 background noise detection"));
    }

    test_begin("radar learning: STATE 6, failed");
    {
        RadarSim radar;
        start(radar);
        radar.auto_outcome = RadarSim::kAutoFailed;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("background noise detection failed (report state 6"));
        CHECK(factory_thresholds(radar));
    }

    test_begin("radar learning: back to normal reports without a result, status 2");
    {
        RadarSim radar;
        start(radar);
        radar.auto_outcome = RadarSim::kAutoEndsSilently;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(radar.count(0x001B) == 1);
    }

    test_begin("radar learning: accepted but not running within 15 s");
    {
        RadarSim radar;
        start(radar);
        radar.auto_outcome = RadarSim::kAutoNeverStarts;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("accepted the background noise detection but does not run it"));
        CHECK(sim_ms() >= 25000 && sim_ms() < 25300);
    }

    test_begin("radar learning: never ends, radar restarted after 10 + 60 + 30 s");
    {
        RadarSim radar;
        start(radar);
        radar.auto_outcome = RadarSim::kAutoNeverEnds;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(radar.restarts == 1 && sim_log_contains("did not end in 100 s, radar restarted"));
    }

    test_begin("radar learning: cancelled while the radar measures, radar restarted, ready 1 s later");
    {
        RadarSim radar;
        start(radar);
        s_cancel_at_ms = 40000;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_CANCELLED);
        CHECK(radar.restarts == 1 && factory_thresholds(radar));
        CHECK(sim_ms() >= 41000 && sim_ms() < 41200);
        CHECK(sim_log_contains("Learning cancelled, radar restarted to stop its detection"));
        CHECK(quiet_learn_restore_factory()); // the radar answers again
    }

    test_begin("radar learning: refused by older firmware, learned on the ESP32 from the end of the delay");
    {
        RadarSim radar;
        start(radar);
        radar.has_auto = false;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_SAVED);
        CHECK(sim_log_contains("This LD2410 firmware has no background noise detection: learning on the ESP32"));
        const int64_t rec = phase_ms(QUIET_LEARN_RECORDING);
        CHECK(rec >= 20000 && rec < 20100);
        CHECK(radar.count(0x0064) == 9);
    }

    test_begin("radar learning: no answer at all");
    {
        RadarSim radar;
        start(radar);
        radar.tx_wire_broken = true;
        CHECK(quiet_learn_run(cancelled, on_phase, 75) == QUIET_LEARN_FAILED);
        CHECK(sim_log_contains("check the wire from GPIO4 to the LD2410 RX pin"));
    }
}

#endif

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
#if CONFIG_LD2410_LEARN_RADAR_AUTO
    printf("quiet_learn (radar learning):\n");
    test_radar_auto();
#elif CONFIG_LD2410_LEARN_PEAK
    printf("quiet_learn (ESP32 learning from the highest energy):\n");
    test_computation();
    test_peak_computation();
    test_peak_learning();
    test_factory_thresholds();
#else
    printf("quiet_learn (ESP32 learning):\n");
    test_computation();
    test_esp32_learning();
    test_factory_thresholds();
#endif
    return test_end();
}
