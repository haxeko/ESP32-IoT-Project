// Host test of main/drivers/pm_sensor.cpp with a PMS5003 on a simulated UART
// (sim.cpp: it stops sending while asleep). Built twice by run.sh: PERIOD=300
// (duty cycle, the default) and PERIOD=0 (continuous measurement).
#include "sim.h"
#include <stdlib.h>

#define CONFIG_PM_SENSOR_MODEL_PMS 1
#define CONFIG_PM_SENSOR_PMS_PERIOD_SEC PERIOD
#include "pm_sensor.cpp"

struct Report {
    int64_t t_ms;
    bool valid;
    float pm2_5;
};
static std::vector<Report> s_reports;

static void on_report(const pm_sensor_data_t *data, void *)
{
    s_reports.push_back({ sim_ms(), data->valid, data->pm2_5 });
}

// 32-byte PMS frame with the given atmospheric PM2.5 value.
static std::vector<uint8_t> pms_frame(uint16_t pm2_5)
{
    std::vector<uint8_t> f = { 0x42, 0x4D, 0x00, 0x1C };
    const uint16_t data[13] = { 1, pm2_5, 9, 1, pm2_5, 9, 0, 0, 0, 0, 0, 0, 0 };
    for (uint16_t d : data) {
        f.push_back(d >> 8);
        f.push_back(d & 0xFF);
    }
    uint16_t sum = 0;
    for (uint8_t b : f) {
        sum += b;
    }
    f.push_back(sum >> 8);
    f.push_back(sum & 0xFF);
    return f;
}

// One frame per second while the sensor is powered: 99 during the 30 s after
// each power-up (warm-up, must never be reported), then `value`.
static void feed(int64_t end_ms, std::vector<std::pair<int64_t, int64_t>> powered, uint16_t value)
{
    for (int64_t t = 500; t < end_ms; t += 1000) {
        for (auto &p : powered) {
            if (t >= p.first && t < p.second) {
                g_sim.uart_rx.push_back({ t, pms_frame(t - p.first < 30000 ? 99 : value) });
            }
        }
    }
}

// step() runs each time the driver waits, e.g. to restart the sensor.
static void run(int64_t end_ms, std::function<void()> step = nullptr)
{
    s_reports.clear();
    s_stats = {};
    g_sim.on_tick = step;
    pm_sensor_config_t config = { .rx_pin = 20, .tx_pin = 21, .report_interval_ms = 10000, .cb = on_report,
                                  .user_data = NULL };
    CHECK(pm_sensor_init(&config) == ESP_OK);
    CHECK(g_sim.gpio_pullup[20] && !g_sim.gpio_pullup[21]);     // RX does not float without the sensor
    try {
        pm_sensor_task(NULL);
    } catch (SimEnd &) {
    }
    CHECK(g_sim.wdt_resets > 0);
    if (getenv("VERBOSE")) {
        for (auto &r : s_reports) printf("    %lld %s %.1f\n", (long long)r.t_ms, r.valid ? "valid" : "invalid", r.pm2_5);
    }
}

static int count_tx(const char *hex)
{
    int n = 0;
    for (auto &s : g_sim.uart_tx) {
        n += s.find(hex) != std::string::npos;
    }
    return n;
}

int main()
{
#if PERIOD > 0
    printf("pm_sensor, duty cycle:\n");
    // Steady sensor: one report per period, 5 s after the 30 s warm-up.
    sim_reset(400000);
    feed(400000, { { 0, 1 << 30 } }, 20);
    run(400000);
    CHECK(s_reports.size() == 2);
    CHECK(s_reports.size() >= 1 && s_reports[0].valid && s_reports[0].pm2_5 == 20 &&
          s_reports[0].t_ms >= 35000 && s_reports[0].t_ms < 35500);
    CHECK(s_reports.size() >= 2 && s_reports[1].valid && s_reports[1].t_ms >= 335000 && s_reports[1].t_ms < 335500);
    CHECK(count_tx("42 4D E4 00 00") == 2);      // sleep after each report
    CHECK(count_tx("42 4D E4 00 01") == 2);      // wake at boot and at 300 s
    pm_sensor_stats_t stats;
    pm_sensor_get_stats(&stats);
    CHECK(stats.reports == 2 && stats.invalid_reports == 0 && stats.wakeups == 2 && stats.sleep_retries == 0);
    CHECK(stats.frames == 70 && stats.rx_bytes == 70 * 32 && stats.checksum_errors == 0 && stats.other_frames == 0 &&
          stats.dropped_bytes == 0);
    CHECK(stats.max_frame_gap_ms >= 1000 && stats.max_frame_gap_ms < 1300);     // not the 265 s asleep

    // The sensor misses the sleep command (lost on the wire): its frames keep
    // coming, so the command is sent again 5 s later, and then it sleeps.
    sim_reset(300000);
    feed(300000, { { 0, 1 << 30 } }, 20);
    g_sim.pms_ignore_sleeps = 1;
    run(300000);
    CHECK(count_tx("42 4D E4 00 00") == 2 && g_sim.pms_asleep);
    CHECK(s_stats.sleep_retries == 1 && sim_log_contains("still sends data after the sleep command"));
    CHECK(s_reports.size() == 1 && s_reports[0].valid);

    // The sensor is plugged in again (awake, fan on) at 100 s, while the
    // driver has it asleep: put to sleep within seconds, not at the next
    // period, and nothing is reported from it.
    sim_reset(300000);
    feed(300000, { { 0, 1 << 30 } }, 20);
    int64_t asleep_again_ms = -1;
    run(300000, [&] {
        if (sim_ms() >= 100000 && sim_ms() < 100200) g_sim.pms_asleep = false;
        if (sim_ms() > 100200 && asleep_again_ms < 0 && g_sim.pms_asleep) asleep_again_ms = sim_ms();
    });
    CHECK(asleep_again_ms > 100200 && asleep_again_ms < 108000);
    CHECK(count_tx("42 4D E4 00 00") == 2 && s_stats.sleep_retries == 1 && s_reports.size() == 1);

    // The sensor never takes the command (TX wire broken): sent again every
    // 5 s, logged once a minute, and the measurements go on. What it sends
    // while it should sleep (77) stays out of them.
    sim_reset(400000);
    for (int64_t t = 500; t < 400000; t += 1000) {
        const bool sampled = (t >= 30000 && t < 35000) || (t >= 330000 && t < 335000);
        g_sim.uart_rx.push_back({ t, pms_frame(sampled ? 20 : 77) });
    }
    g_sim.pms_ignore_sleeps = 1000;
    run(400000);
    CHECK(s_stats.sleep_retries >= 60 && s_stats.sleep_retries <= 66);
    int logged = 0;
    for (const std::string &line : g_sim.log) {
        logged += line.find("still sends data") != std::string::npos;
    }
    CHECK(logged >= 5 && logged <= 7);
    CHECK(s_reports.size() == 2 && s_reports[0].pm2_5 == 20 && s_reports[1].valid && s_reports[1].pm2_5 == 20);

    // Silent sensor: invalid report once per period.
    sim_reset(70000);
    run(70000);
    CHECK(s_reports.size() == 1 && !s_reports[0].valid);

    // Noise with a false header (every 3 s), a command answer (every 4 s) and
    // frames split in two reads: each costs at most the next frame, and
    // nothing invalid is ever reported.
    sim_reset(40000);
    for (int64_t t = 500; t < 40000; t += 1000) {
        std::vector<uint8_t> f = pms_frame(t < 30500 ? 99 : 40);
        if (t % 3000 == 500) {
            g_sim.uart_rx.push_back({ t, { 0x00, 0x42, 0x42, 0x4D, 0x00 } });
        }
        if (t % 4000 == 1500) {
            g_sim.uart_rx.push_back({ t, { 0x42, 0x4D, 0x00, 0x04, 0xE1, 0x00, 0x01, 0x74 } });
        }
        g_sim.uart_rx.push_back({ t, std::vector<uint8_t>(f.begin(), f.begin() + 7) });
        g_sim.uart_rx.push_back({ t + 50, std::vector<uint8_t>(f.begin() + 7, f.end()) });
    }
    run(40000);
    CHECK(s_reports.size() == 1 && s_reports[0].valid && s_reports[0].pm2_5 == 40);
    // The command answers are not counted as checksum errors, the false headers as dropped bytes.
    CHECK(s_stats.checksum_errors == 0 && s_stats.other_frames >= 5 && s_stats.dropped_bytes > 0);

    // A frame with a wrong checksum is dropped and counted.
    sim_reset(40000);
    for (int64_t t = 500; t < 40000; t += 1000) {
        std::vector<uint8_t> f = pms_frame(t < 30500 ? 99 : 40);
        if (t == 32500) {
            f[10] ^= 0x01;      // PM1.0 changed on the wire
        }
        g_sim.uart_rx.push_back({ t, f });
    }
    run(40000);
    CHECK(s_stats.checksum_errors == 1 && s_stats.other_frames == 0 && s_stats.frames == 34);
    CHECK(s_reports.size() == 1 && s_reports[0].valid && s_reports[0].pm2_5 == 40);
#else
    printf("pm_sensor, continuous:\n");
    // Present from boot: first report after the warm-up, then every 10 s.
    sim_reset(70000);
    feed(70000, { { 0, 1 << 30 } }, 20);
    run(70000);
    CHECK(s_reports.size() == 3);
    for (auto &r : s_reports) {
        CHECK(r.valid && r.pm2_5 == 20 && r.t_ms >= 30000);
    }

    // Plugged in at 100 s: invalid once, setup retried while silent, then the
    // warm-up after the first frame is skipped again.
    sim_reset(160000);
    feed(160000, { { 100000, 1 << 30 } }, 20);
    run(160000);
    CHECK(!s_reports.empty() && !s_reports[0].valid);
    int invalid = 0;
    for (auto &r : s_reports) {
        invalid += !r.valid;
        CHECK(!r.valid || (r.pm2_5 == 20 && r.t_ms >= 130000));
    }
    CHECK(invalid == 1);
    CHECK(count_tx("42 4D E4 00 01") >= 3);      // wake at boot + retries

    // Unplugged from 60 s to 150 s.
    sim_reset(220000);
    feed(220000, { { 0, 60000 }, { 150000, 1 << 30 } }, 20);
    run(220000);
    invalid = 0;
    for (auto &r : s_reports) {
        invalid += !r.valid;
        CHECK(!r.valid || r.pm2_5 == 20);
        CHECK(!r.valid || r.t_ms < 61000 || r.t_ms >= 180000);
    }
    CHECK(invalid == 1);
    CHECK(s_reports.back().valid && s_reports.back().t_ms >= 200000);
#endif
    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
