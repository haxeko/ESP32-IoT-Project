// Radar -> occupancy (main/presence.cpp) with the driver and the simulated radar: changes only, retries, silent
// radar, target states 4-6. Built twice: CONFIG_PRESENCE_MAX_DISTANCE_CM 0 (default) and 200.
#include "sim.h"

// Both files in this test so it can reset the driver between boots; each has its own TAG
#define TAG TAG_ld2410
#include "ld2410.cpp"
#undef TAG
#include "presence.cpp"

#include "radar_sim.h"

static std::vector<std::pair<int64_t, bool>> s_reports;
static bool s_accept = true;

static bool report(bool present)
{
    if (!s_accept) {
        return false;
    }
    s_reports.push_back({ sim_ms(), present });
    return true;
}

static void start(RadarSim &radar)
{
    sim_reset();
    // A new boot: no report received yet
    s_has_target = false;
    s_target = {};
    s_parser = {};
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.attach();
    s_reports.clear();
    s_accept = true;
    CHECK(presence_start(report) == ESP_OK);
}

static void run(int64_t end_ms)
{
    g_sim.end_ms = end_ms;
    try {
        presence_task(nullptr);
    } catch (SimEnd &) {
    }
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("presence (max distance %d cm):\n", CONFIG_PRESENCE_MAX_DISTANCE_CM);

    test_begin("presence_from_target: moving or still, within the distance when set");
    {
        ld2410_target_t t = {};
        t.moving = true;
        t.moving_cm = 300;
        CHECK(presence_from_target(&t, 0) && !presence_from_target(&t, 200) && presence_from_target(&t, 300));
        t = {};
        t.still = true;
        t.still_cm = 150;
        CHECK(presence_from_target(&t, 0) && presence_from_target(&t, 200));
        t.state = 4; // noise detection: the driver never sets moving / still then
        t.still = false;
        CHECK(!presence_from_target(&t, 0));
    }

#if CONFIG_PRESENCE_MAX_DISTANCE_CM == 0
    test_begin("first state reported once, then only the changes");
    {
        RadarSim radar;
        start(radar);
        sim_at(3000, [&radar]() {
            radar.target_state = 1;
            radar.moving_cm = 120;
        });
        sim_at(6000, [&radar]() { radar.target_state = 2; radar.still_cm = 90; radar.moving_cm = 0; });
        sim_at(9000, [&radar]() { radar.target_state = 0; });
        run(12000);
        CHECK(s_reports.size() == 3);
        CHECK(!s_reports[0].second && s_reports[0].first <= 200);
        CHECK(s_reports[1].second && s_reports[1].first >= 3000 && s_reports[1].first < 3300);
        CHECK(!s_reports[2].second && s_reports[2].first >= 9000 && s_reports[2].first < 9300);
        CHECK(sim_log_contains("Radar: presence (moving yes 120 cm, still no 0 cm)"));
        CHECK(sim_log_contains("Radar: no one (moving no 0 cm, still no 90 cm)"));
    }

    test_begin("a report that does not reach the cluster is retried at the next poll");
    {
        RadarSim radar;
        start(radar);
        s_accept = false;
        sim_at(1000, []() { s_accept = true; });
        sim_at(2000, [&radar]() { radar.target_state = 3; radar.moving_cm = 50; radar.still_cm = 60; });
        run(4000);
        CHECK(s_reports.size() == 2 && s_reports[0].first >= 1000 && s_reports[0].first <= 1100 && s_reports[1].second);
    }

    test_begin("silent radar: no one after 10 s with an error, data again later");
    {
        RadarSim radar;
        start(radar);
        radar.target_state = 1;
        radar.moving_cm = 200;
        sim_at(5000, [&radar]() { radar.rx_wire_broken = true; });
        sim_at(30000, [&radar]() { radar.rx_wire_broken = false; });
        run(35000);
        CHECK(sim_log_contains("No data from the LD2410 for 10 s, check the wiring (radar TX -> GPIO3, RX -> GPIO4)"));
        CHECK(sim_log_contains("Radar: no one (no radar data)") && sim_log_contains("Radar data received"));
        CHECK(s_reports.size() == 3 && s_reports[0].second && !s_reports[1].second && s_reports[2].second);
        CHECK(s_reports[1].first >= 15000 && s_reports[1].first < 15200);
    }

    test_begin("no radar at all: the grace period counts from boot");
    {
        RadarSim radar;
        start(radar);
        radar.rx_wire_broken = true;
        run(12000);
        CHECK(sim_log_count("No data from the LD2410 for 10 s") == 1);
        CHECK(s_reports.size() == 1 && !s_reports[0].second);
    }

    test_begin("noise detection states 4-6 are not presence");
    {
        RadarSim radar;
        start(radar);
        radar.target_state = 1;
        radar.moving_cm = 100;
        sim_at(2000, [&radar]() { // as if the radar had started its detection
            radar.auto_start_ms = sim_ms();
            radar.auto_seconds = 10;
        });
        run(25000);
        CHECK(s_reports.size() >= 2 && s_reports[0].second && !s_reports[1].second);
        CHECK(s_reports[1].first >= 2000 && s_reports[1].first < 2300);
    }
#else
    test_begin("targets further than the distance are not counted");
    {
        RadarSim radar;
        start(radar);
        radar.target_state = 1;
        radar.moving_cm = 350;
        sim_at(3000, [&radar]() { radar.moving_cm = 150; });
        run(5000);
        CHECK(s_reports.size() == 2 && !s_reports[0].second && s_reports[1].second);
    }
#endif

    return test_end();
}
