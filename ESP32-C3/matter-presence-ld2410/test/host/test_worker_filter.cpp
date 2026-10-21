// The radar_cfg task with the ESP32 energy filter (CONFIG_PRESENCE_ENERGY_FILTER) against the simulated radar: the
// filter gets the radar's thresholds, Range and gate length, the engineering mode is turned on and turned on again
// after a radar restart or a learning on the ESP32, the thresholds are read again after a learning or the factory
// thresholds.
#include "sim.h"

#include "radar_worker.cpp"
#include "energy_filter.cpp"

#include <string.h>

#include "radar_sim.h"

void status_led_set_learning(status_led_learning_t learning) {}

static void view_settings(const radar_settings_t *settings) {}
static void view_learn_mode(uint8_t mode) {}

// New boot of the ESP32 with a radar whose Bluetooth is off (no restart at boot)
static void boot(RadarSim &radar)
{
    sim_reset(true);
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.bluetooth = false;
    radar.stored_bluetooth = false;
    radar.attach();
    s_learn_state = kLearnIdle;
    s_want_factory = false;
    s_want_gate = RADAR_KEEP;
    s_want_gate_cm = RADAR_KEEP;
    s_want_bluetooth = RADAR_KEEP;
    s_synced = false;
    s_task = nullptr;
    s_view = {};
    s_bt_wanted = false;
    s_bt_change_failed = false;
    s_settings = {};
    s_session_err = ESP_OK;
    s_engineering_tried_ms = -kEngineeringRetryMs;
    s_thresholds_changed = false;
    s_configured = false;
    s_cfg = {};
    s_state = {};
    s_event_pending = false;
    s_seen = false;
    energy_filter_start();
}

static void run_worker(int64_t end_ms)
{
    static const radar_worker_view_t view = { view_settings, view_learn_mode };
    CHECK(radar_worker_start(&view) == ESP_OK);
    g_sim.end_ms = end_ms;
    try {
        radar_task(nullptr);
    } catch (SimEnd &) {
    }
}

static bool filter_has(const uint8_t moving[9], const uint8_t still[9])
{
    return s_configured && memcmp(s_cfg.moving_threshold, moving, 9) == 0 &&
           memcmp(s_cfg.still_threshold + 2, still + 2, 7) == 0;
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("radar worker with the energy filter:\n");

    test_begin("boot: thresholds, Range and gate length to the filter, then the engineering mode on");
    {
        RadarSim radar;
        boot(radar);
        radar.max_moving_gate = 5;
        radar.max_still_gate = 4;
        run_worker(5000);
        CHECK(filter_has(radar.moving, radar.still));
        CHECK(s_cfg.max_moving_gate == 5 && s_cfg.max_still_gate == 4 && s_cfg.gate_cm == 75);
        CHECK(s_cfg.strong == 15 && s_cfg.frames == 3 && s_cfg.max_cm == CONFIG_PRESENCE_MAX_DISTANCE_CM);
        CHECK(radar.engineering && radar.count(0x0062) == 1);
        CHECK(sim_log_count("Engineering mode on: the energy of every gate for the energy filter") == 1);
    }

    test_begin("Range changed: the filter follows");
    {
        RadarSim radar;
        boot(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_RANGE, 3); });
        run_worker(8000);
        CHECK(radar.max_moving_gate == 3 && s_cfg.max_moving_gate == 3 && s_cfg.max_still_gate == 3);
        CHECK(radar.engineering);
    }

    test_begin("radar restarted (Radar resolution): engineering mode on again, at most every 10 s");
    {
        RadarSim radar;
        boot(radar);
        sim_at(15000, []() { radar_worker_select(RADAR_SELECT_RESOLUTION, RADAR_RESOLUTION_FINE); });
        run_worker(22000);
        CHECK(radar.restarts == 1 && s_cfg.gate_cm == 20);
        CHECK(radar.engineering && radar.count(0x0062) == 2);
    }

    test_begin("radar power cut (restart): engineering mode on again within a few seconds");
    {
        RadarSim radar;
        boot(radar);
        sim_at(15000, [&radar]() { radar.power_cut(); });
        run_worker(20000);
        CHECK(radar.engineering && radar.count(0x0062) == 2);
    }

    test_begin("radar refusing the engineering mode: warning, tried again every 10 s");
    {
        RadarSim radar;
        boot(radar);
        radar.refuse.insert(0x0062);
        run_worker(25000);
        CHECK(!radar.engineering && radar.count(0x0062) == 3);
        CHECK(sim_log_contains("Could not turn the engineering mode on (err:"));
        CHECK(sim_log_contains("the radar decides alone meanwhile"));
    }

    test_begin("learning by the radar: its new thresholds to the filter");
    {
        RadarSim radar;
        boot(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        run_worker(120000);
        CHECK(radar.moving[0] == 45 && filter_has(radar.auto_moving, radar.auto_still));
        CHECK(radar.engineering);
    }

    test_begin("learning on the ESP32: new thresholds to the filter, engineering mode on again after it");
    {
        RadarSim radar;
        boot(radar);
        radar.has_auto = false;
        radar.energies = [](int64_t, uint8_t *m, uint8_t *s) {
            m[3] = 40; // empty room noise: threshold of gate 3 raised to 40 + 10
            s[3] = 45;
        };
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        run_worker(120000);
        CHECK(radar.moving[3] == 50 && radar.still[3] == 55 && filter_has(radar.moving, radar.still));
        CHECK(radar.engineering && sim_log_contains("Learning done: new thresholds saved in the LD2410"));
    }

    test_begin("factory thresholds: the filter gets them");
    {
        RadarSim radar;
        boot(radar);
        radar.moving[3] = 60;
        radar.still[3] = 70;
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        run_worker(20000);
        static const uint8_t moving[9] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
        static const uint8_t still[9] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };
        CHECK(filter_has(moving, still) && radar.engineering);
    }

    return test_end();
}
