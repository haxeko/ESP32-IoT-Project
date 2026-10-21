// ESP32 energy filter (main/energy_filter.h, energy_filter.cpp) and its use by the presence task
// (CONFIG_PRESENCE_ENERGY_FILTER): an energy far above its threshold makes the empty room occupied at once, weaker
// ones need several reports above in a row; once occupied the radar decides alone; the filter never adds a
// detection. Then the driver, presence task and filter together against the simulated radar.
#include "sim.h"

// All in this test so it can reset the driver and the filter between runs; each file has its own TAG
#define TAG TAG_ld2410
#include "ld2410.cpp"
#undef TAG
#include "presence.cpp"
#include "energy_filter.cpp"

#include "radar_sim.h"

// Factory thresholds of the LD2410, Range 6 m (gate 8), 0.75 m gates
static const uint8_t kMoving[9] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kStill[9] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };

static energy_filter_config_t config(uint16_t max_cm = 0)
{
    energy_filter_config_t cfg = {};
    memcpy(cfg.moving_threshold, kMoving, sizeof(kMoving));
    memcpy(cfg.still_threshold, kStill, sizeof(kStill));
    cfg.max_moving_gate = 8;
    cfg.max_still_gate = 8;
    cfg.gate_cm = 75;
    cfg.max_cm = max_cm;
    cfg.strong = 15;
    cfg.frames = 3;
    return cfg;
}

// A report of the radar: target state (0 none, 1 moving, 2 still, 3 both, 4-6 noise detection), every energy 5
static ld2410_report_t quiet(uint8_t state)
{
    ld2410_report_t r = {};
    r.target.state = state;
    r.target.moving = state <= 3 && (state & 1) != 0;
    r.target.still = state <= 3 && (state & 2) != 0;
    r.target.moving_cm = 150;
    r.target.still_cm = 150;
    r.engineering = true;
    r.moving_gates = 9;
    r.still_gates = 9;
    memset(r.moving_energy, 5, sizeof(r.moving_energy));
    memset(r.still_energy, 5, sizeof(r.still_energy));
    return r;
}

static ld2410_report_t moving(uint8_t gate, uint8_t energy, uint8_t state = 1)
{
    ld2410_report_t r = quiet(state);
    r.moving_energy[gate] = energy;
    return r;
}

static ld2410_report_t still(uint8_t gate, uint8_t energy, uint8_t state = 2)
{
    ld2410_report_t r = quiet(state);
    r.still_energy[gate] = energy;
    return r;
}

// Decisions of a sequence of reports: "#" present, "." not
static std::string run_reports(energy_filter_state_t *s, const energy_filter_config_t &cfg,
                               const std::vector<ld2410_report_t> &reports)
{
    std::string out;
    for (const ld2410_report_t &r : reports) {
        out += energy_filter_step(s, &cfg, &r) ? '#' : '.';
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Driver + presence task + filter against the simulated radar

static std::vector<std::pair<int64_t, bool>> s_reports;

static bool report_cb(bool present)
{
    s_reports.push_back({ sim_ms(), present });
    return true;
}

// The filter gets the simulated radar's thresholds and Range, as the radar_cfg task does
static void configure_from(const RadarSim &radar)
{
    radar_settings_t settings = {};
    memcpy(settings.config.thresholds.moving, radar.moving, sizeof(radar.moving));
    memcpy(settings.config.thresholds.still, radar.still, sizeof(radar.still));
    settings.config.max_moving_gate = radar.max_moving_gate;
    settings.config.max_still_gate = radar.max_still_gate;
    settings.gate_cm = 75;
    energy_filter_configure(&settings);
}

static void start(RadarSim &radar, bool configure)
{
    sim_reset();
    s_has_target = false;
    s_target = {};
    s_parser = {};
    s_configured = false;
    s_state = {};
    s_event = {};
    s_event_pending = false;
    s_seen = false;
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.attach();
    radar.engineering = true;
    s_reports.clear();
    CHECK(presence_start(report_cb) == ESP_OK);
    if (configure) {
        configure_from(radar);
    }
}

static void run(int64_t end_ms)
{
    g_sim.end_ms = end_ms;
    try {
        presence_task(nullptr);
    } catch (SimEnd &) {
    }
}

// The radar reports a moving target from the report at from_ms (set 50 ms before it) and holds it for its no-one
// duration (5 s) after the last report above a threshold
static void radar_target(RadarSim &radar, int64_t from_ms, int64_t last_above_ms)
{
    sim_at(from_ms - 50, [&radar]() {
        radar.target_state = 1;
        radar.moving_cm = 225;
    });
    sim_at(last_above_ms + 5000 - 50, [&radar]() { radar.target_state = 0; });
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("energy_filter (strong %d, %d reports):\n", CONFIG_PRESENCE_ENERGY_FILTER_STRONG,
           CONFIG_PRESENCE_ENERGY_FILTER_FRAMES);
    const energy_filter_config_t cfg = config();

    test_begin("an energy at least 15 above its threshold: occupied at once");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { quiet(1), moving(3, 30 + 20) }) == ".#");
        CHECK(s.event == ENERGY_FILTER_STRONG && s.event_frames == 1);
        CHECK(s.peak.gate == 3 && s.peak.moving && s.peak.energy == 50 && s.peak.threshold == 30 &&
              s.peak.excess == 20);
        s = {};
        CHECK(run_reports(&s, cfg, { still(4, 30 + 15) }) == "#"); // exactly 15 above
        s = {};
        CHECK(run_reports(&s, cfg, { still(4, 30 + 14) }) == "."); // 14: a weaker one
    }

    test_begin("one report slightly above (noise): ignored");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 35), quiet(1), quiet(1) }) == "...");
        CHECK(s.event == ENERGY_FILTER_NO_EVENT); // the event was on the second report
        s = {};
        run_reports(&s, cfg, { moving(3, 35), quiet(1) });
        CHECK(s.event == ENERGY_FILTER_IGNORED && s.event_frames == 1 && s.peak.gate == 3 && s.peak.excess == 5);
    }

    test_begin("weaker ones: 3 reports above in a row, a report below starts the count again");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 35), moving(3, 38), quiet(1), moving(3, 33), moving(3, 31) }) ==
              ".....");
        CHECK(s.weak_frames == 2);
        CHECK(run_reports(&s, cfg, { moving(3, 32) }) == "#");
        CHECK(s.event == ENERGY_FILTER_CONFIRMED && s.event_frames == 3 && s.peak.excess == 3); // highest of 3, 1, 2
        s = {};
        run_reports(&s, cfg, { moving(3, 35), moving(3, 38) });
        run_reports(&s, cfg, { quiet(1) });
        CHECK(s.event == ENERGY_FILTER_IGNORED && s.event_frames == 2 && s.peak.excess == 8);
    }

    test_begin("a weak count that turns strong: occupied at once");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 34), moving(3, 60) }) == ".#");
        CHECK(s.event == ENERGY_FILTER_STRONG && s.event_frames == 2 && s.peak.excess == 30);
    }

    test_begin("the radar's target held for its no-one duration after a filtered report: still empty");
    {
        energy_filter_state_t s = {};
        std::vector<ld2410_report_t> reports = { moving(3, 35) };
        for (int i = 0; i < 50; i++) {
            reports.push_back(quiet(1)); // target held, energies back to 5
        }
        CHECK(run_reports(&s, cfg, reports) == std::string(51, '.'));
    }

    test_begin("never more than the radar: energies above with no target from the radar");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 90, 0), moving(3, 90, 0), moving(3, 90, 0), moving(3, 90, 0) }) ==
              "....");
    }

    test_begin("occupied: kept while the radar reports a target, whatever the energies; then confirmed again");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 60), quiet(1), quiet(2), quiet(3), quiet(0), quiet(1) }) == "####..");
        CHECK(run_reports(&s, cfg, { moving(3, 35), moving(3, 35), moving(3, 35) }) == "..#");
    }

    test_begin("gates counted: Range, still gates 2 and up, a threshold of 100 never passed");
    {
        energy_filter_config_t short_range = cfg;
        short_range.max_moving_gate = 3;
        short_range.max_still_gate = 3;
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, short_range, { moving(4, 90), moving(4, 90), moving(4, 90) }) == "...");
        CHECK(run_reports(&s, short_range, { moving(3, 90) }) == "#");
        energy_filter_config_t still_short = cfg; // moving 6 m, still 2.25 m
        still_short.max_still_gate = 3;
        s = {};
        CHECK(run_reports(&s, still_short, { still(4, 90), still(4, 90), still(4, 90) }) == "...");
        CHECK(run_reports(&s, still_short, { moving(4, 90) }) == "#");
        s = {};
        // Still gates 0 and 1 (threshold 0, cannot be set): never counted, though every quiet energy is 5
        CHECK(run_reports(&s, cfg, { still(0, 100), still(1, 100), still(0, 100), quiet(2) }) == "....");
        CHECK(run_reports(&s, cfg, { still(2, 100) }) == "#");
        energy_filter_config_t ignored = cfg;
        ignored.moving_threshold[5] = 100;
        s = {};
        CHECK(run_reports(&s, ignored, { moving(5, 100), moving(5, 100), moving(5, 100) }) == "...");
        // Fewer gates in the report than in Range
        ld2410_report_t r = moving(6, 90);
        r.moving_gates = 6;
        r.still_gates = 6;
        s = {};
        CHECK(run_reports(&s, cfg, { r, r, r }) == "...");
    }

    test_begin("CONFIG_PRESENCE_MAX_DISTANCE_CM 200: gates starting farther are not counted");
    {
        const energy_filter_config_t near = config(200);
        energy_filter_state_t s = {};
        ld2410_report_t r = moving(3, 90); // gate 3 starts at 225 cm
        r.target.moving_cm = 150;
        CHECK(run_reports(&s, near, { r, r, r }) == "...");
        r = moving(2, 90); // 150 cm
        r.target.moving_cm = 150;
        CHECK(run_reports(&s, near, { r }) == "#");
        r.target.moving_cm = 250; // the radar's target too far
        CHECK(run_reports(&s, near, { r }) == ".");
    }

    test_begin("no energies (engineering mode off): the radar decides alone");
    {
        energy_filter_state_t s = {};
        ld2410_report_t basic = quiet(1);
        basic.engineering = false;
        CHECK(run_reports(&s, cfg, { basic, basic }) == "##");
        basic = quiet(0);
        basic.engineering = false;
        CHECK(run_reports(&s, cfg, { basic }) == ".");
        CHECK(run_reports(&s, cfg, { moving(3, 35) }) == "."); // energies again: filtered again
    }

    test_begin("noise detection states 4-6: no one");
    {
        energy_filter_state_t s = {};
        CHECK(run_reports(&s, cfg, { moving(3, 90, 4), moving(3, 90, 5), moving(3, 90, 6) }) == "...");
    }

    test_begin("presence task: one weak report and the held target ignored, logged");
    {
        RadarSim radar;
        start(radar, true);
        radar.energies = [](int64_t ms, uint8_t *m, uint8_t *) {
            if (ms == 1000) {
                m[3] = 35; // gate 3, threshold 30
            }
        };
        radar_target(radar, 1000, 1000);
        run(8000);
        CHECK(s_reports.size() == 1 && !s_reports[0].second);
        CHECK(sim_log_contains("Energy filter: ignored 1 report above a threshold (highest moving gate 3 energy 35, "
                               "threshold 30, +5)"));
    }

    test_begin("presence task: a strong report occupies at once, the radar then decides alone");
    {
        RadarSim radar;
        start(radar, true);
        radar.energies = [](int64_t ms, uint8_t *m, uint8_t *) {
            if (ms == 2000) {
                m[3] = 70;
            }
        };
        radar_target(radar, 2000, 2000);
        run(10000);
        CHECK(s_reports.size() == 3 && s_reports[1].second && !s_reports[2].second);
        CHECK(s_reports[1].first >= 2000 && s_reports[1].first <= 2100);
        CHECK(s_reports[2].first >= 7000 && s_reports[2].first <= 7100); // the radar's no-one duration (report at 7000)
        CHECK(sim_log_contains("Energy filter: moving gate 3 energy 70, threshold 30 (+40): occupied at once"));
    }

    test_begin("presence task: radar settings read again while occupied (Range): the decision is kept");
    {
        RadarSim radar;
        start(radar, true);
        radar.energies = [](int64_t ms, uint8_t *m, uint8_t *) {
            if (ms == 2000) {
                m[3] = 70;
            }
        };
        radar_target(radar, 2000, 2000);
        sim_at(3000, [&radar]() {
            radar.max_moving_gate = 6;
            configure_from(radar);
        });
        run(6000);
        CHECK(s_reports.size() == 2 && s_reports[1].second && s_cfg.max_moving_gate == 6);
    }

    test_begin("presence task: weak reports in a row occupy after the third (about 0.3 s)");
    {
        RadarSim radar;
        start(radar, true);
        radar.energies = [](int64_t ms, uint8_t *, uint8_t *s) {
            if (ms >= 3000 && ms < 6000) {
                s[4] = 36; // gate 4 still, threshold 30
            }
        };
        sim_at(2950, [&radar]() {
            radar.target_state = 2;
            radar.still_cm = 300;
        });
        run(5000);
        CHECK(s_reports.size() == 2 && s_reports[1].second);
        CHECK(s_reports[1].first >= 3200 && s_reports[1].first <= 3300);
        CHECK(sim_log_contains("Energy filter: 3 reports in a row above a threshold (highest still gate 4 energy 36, "
                               "threshold 30, +6): occupied"));
    }

    test_begin("presence task: not configured yet, or no energies: the radar decides alone");
    {
        RadarSim radar;
        start(radar, false);
        radar_target(radar, 1000, 1000);
        run(3000);
        CHECK(s_reports.size() == 2 && s_reports[1].second && s_reports[1].first <= 1200);
        RadarSim basic;
        start(basic, true);
        basic.engineering = false;
        radar_target(basic, 1000, 1000);
        run(3000);
        CHECK(s_reports.size() == 2 && s_reports[1].second && s_reports[1].first <= 1200);
        CHECK(energy_filter_needs_engineering(sim_ms()));
        CHECK(!energy_filter_needs_engineering(sim_ms() + 3000)); // no report for more than 2 s
    }

    return test_end();
}
