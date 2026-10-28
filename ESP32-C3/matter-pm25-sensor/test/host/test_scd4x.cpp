// Host test of main/drivers/scd4x.cpp with an SCD40/SCD41 simulated on the
// I2C bus (sim.cpp): start-up, averaging, invalid samples, and recovery when
// the sensor is missing, unplugged, reset or stuck.
#include "sim.h"
#include <math.h>
#include <stdlib.h>

#include "scd4x.cpp"

struct Report {
    int64_t t_ms;
    bool valid;
    float co2;
};
static std::vector<Report> s_reports;

static void on_report(const scd4x_data_t *data, void *)
{
    s_reports.push_back({ sim_ms(), data->valid, data->co2_ppm });
}

static int count_commands(uint16_t cmd)
{
    int n = 0;
    for (uint16_t c : g_sim.scd4x_commands) {
        n += c == cmd;
    }
    return n;
}

static int invalid_reports()
{
    int n = 0;
    for (auto &r : s_reports) {
        n += !r.valid;
    }
    return n;
}

// Runs the sensor task until end_ms, calling step() each time it waits.
// before() runs after the simulator reset (state of the sensor at boot).
static void run(int64_t end_ms, uint32_t interval_ms, std::function<void()> step = nullptr,
                std::function<void()> before = nullptr)
{
    sim_reset(end_ms);
    s_reports.clear();
    if (before) {
        before();
    }
    g_sim.on_tick = step;
    scd4x_config_t config = { .bus = (void *)1, .report_interval_ms = interval_ms, .cb = on_report,
                              .user_data = NULL };
    CHECK(scd4x_init(&config) == ESP_OK);
    try {
        scd4x_task(NULL);
    } catch (SimEnd &) {
    }
    CHECK(g_sim.wdt_resets > 0);
    CHECK(g_sim.scd4x_violations == 0);
    if (getenv("VERBOSE")) {
        for (auto &r : s_reports) printf("    %lld %s %.1f\n", (long long)r.t_ms, r.valid ? "valid" : "invalid", r.co2);
    }
}

int main()
{
    printf("scd4x:\n");
    // CRC example of the datasheet.
    const uint8_t beef[] = { 0xBE, 0xEF };
    CHECK(crc8(beef, 2) == 0x92 && sim_crc8(beef, 2) == 0x92);

    scd4x_config_t config = { .bus = (void *)1, .report_interval_ms = 30000, .cb = on_report, .user_data = NULL };
    CHECK(scd4x_init(NULL) == ESP_ERR_INVALID_ARG);
    config.cb = NULL;
    CHECK(scd4x_init(&config) == ESP_ERR_INVALID_ARG);
    config.cb = on_report;
    config.report_interval_ms = 0;
    CHECK(scd4x_init(&config) == ESP_ERR_INVALID_ARG);
    config.report_interval_ms = 30000;
    config.bus = NULL;
    CHECK(scd4x_init(&config) == ESP_ERR_INVALID_STATE);

    // Steady sensor: stopped, identified and started once, then one report
    // every 30 s.
    run(100000, 30000);
    CHECK(s_reports.size() == 3);
    for (size_t i = 0; i < s_reports.size(); i++) {
        CHECK(s_reports[i].valid && s_reports[i].co2 == 600);
        CHECK(s_reports[i].t_ms >= 31000 + 30000 * (int64_t)i && s_reports[i].t_ms < 33000 + 30000 * (int64_t)i);
    }
    CHECK(g_sim.scd4x_commands.size() >= 3 && g_sim.scd4x_commands[0] == 0x3F86 &&
          g_sim.scd4x_commands[1] == 0x3682 && g_sim.scd4x_commands[2] == 0x21B1);
    CHECK(count_commands(0x21B1) == 1 && count_commands(0x3F86) == 1);
    CHECK(sim_log_contains("CO2 sensor 123456789ABC started") && sim_log_contains("CO2=600 ppm (sensor 25 C, 50 %RH)"));

    // 5 s interval: every measurement is reported.
    run(60000, 5000);
    CHECK(s_reports.size() >= 10);

    // Average of the interval, in whole ppm: 500 ppm, then 800 ppm from 15 s.
    run(40000, 30000, [] { g_sim.scd4x_co2 = sim_ms() < 15000 ? 500 : 800; });
    CHECK(s_reports.size() == 1 && s_reports[0].valid && s_reports[0].co2 > 500 && s_reports[0].co2 < 800 &&
          s_reports[0].co2 == roundf(s_reports[0].co2));

    // Still measuring at boot (the ESP32 restarted, not the sensor): stopped
    // first, so that it accepts the other commands.
    run(40000, 30000, nullptr, [] {
        g_sim.scd4x_measuring = true;
        g_sim.scd4x_start_ms = -100000;
    });
    CHECK(s_reports.size() == 1 && s_reports[0].valid && count_commands(0x21B1) == 1);

    // 0 ppm (invalid sample) is skipped.
    run(40000, 30000, [] { g_sim.scd4x_co2 = g_sim.scd4x_read < 2 ? 0 : 700; });
    CHECK(s_reports.size() == 1 && s_reports[0].valid && s_reports[0].co2 == 700);

    // Measurements with a wrong CRC are skipped, without restarting the sensor.
    run(40000, 30000, nullptr, [] { g_sim.scd4x_corrupt = 2; });
    CHECK(g_sim.scd4x_corrupt == 0);
    CHECK(s_reports.size() == 1 && s_reports[0].valid && s_reports[0].co2 == 600 && count_commands(0x21B1) == 1);

    // Missing at boot, plugged in at 50 s: reported missing once (after
    // 30 s), only probed meanwhile (no command, so no I2C error logs), then
    // started and reported at once.
    run(100000, 30000, [] { g_sim.scd4x_present = sim_ms() >= 50000; });
    CHECK(invalid_reports() == 1 && !s_reports.empty() && !s_reports[0].valid);
    CHECK(!s_reports.empty() && s_reports[0].t_ms >= 30000 && s_reports[0].t_ms < 33000);
    CHECK(s_reports.size() >= 2 && s_reports[1].valid && s_reports[1].co2 == 600 && s_reports[1].t_ms < 65000);
    CHECK(count_commands(0x3F86) == 1 && count_commands(0x21B1) == 1 && g_sim.scd4x_unanswered == 0);
    CHECK(g_sim.scd4x_probes >= 5 && g_sim.scd4x_probes <= 7);     // every 10 s
    CHECK(sim_log_contains("CO2 sensor not answering"));

    // Unplugged from 60 s to 120 s: one failed read, then only probed;
    // reported missing once, 30 s after the last measurement, then started again.
    run(180000, 30000, [] { g_sim.scd4x_present = !(sim_ms() >= 60000 && sim_ms() < 120000); });
    CHECK(g_sim.scd4x_unanswered == 1);
    CHECK(invalid_reports() == 1);
    for (auto &r : s_reports) {
        CHECK(r.valid ? (r.co2 == 600 && (r.t_ms < 62000 || r.t_ms >= 120000)) : (r.t_ms >= 85000 && r.t_ms < 92000));
    }
    CHECK(!s_reports.empty() && s_reports.back().valid && s_reports.back().t_ms >= 120000);
    CHECK(count_commands(0x21B1) == 2);

    // Reset by a power glitch at 60 s (idle again): started again after 15 s
    // without a measurement, before it would be reported missing.
    bool glitched = false;
    run(120000, 30000, [&] {
        if (sim_ms() >= 60000 && !glitched) {
            glitched = true;
            g_sim.scd4x_measuring = false;
        }
    });
    CHECK(invalid_reports() == 0 && count_commands(0x21B1) == 2);
    CHECK(!s_reports.empty() && s_reports.back().valid && s_reports.back().t_ms >= 90000);
    CHECK(sim_log_contains("restarting the sensor"));

    // Answers but never measures: started again every 15 s, reported missing once.
    run(100000, 30000, [] { g_sim.scd4x_measuring = false; });
    CHECK(s_reports.size() == 1 && invalid_reports() == 1);
    CHECK(count_commands(0x21B1) >= 6);

    // Report interval longer than the timeout: the measurements before a
    // missing period (900 ppm) are not mixed into the next report.
    run(150000, 120000, [] {
        g_sim.scd4x_present = !(sim_ms() >= 60000 && sim_ms() < 100000);
        g_sim.scd4x_co2 = sim_ms() < 60000 ? 900 : 600;
    });
    CHECK(s_reports.size() == 2 && !s_reports[0].valid);
    CHECK(s_reports.size() == 2 && s_reports[1].valid && s_reports[1].co2 == 600);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
