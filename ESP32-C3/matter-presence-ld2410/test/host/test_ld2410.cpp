// HLK-LD2410 driver (main/drivers/ld2410.cpp): frame parser and configuration commands against the simulated radar.
#include "sim.h"

#include "ld2410.cpp"
#include "radar_sim.h"

#include <string.h>

// Target report frame: type 0x02 basic or 0x01 engineering (gates 0..8, energies e).
static std::vector<uint8_t> report(uint8_t type, uint8_t state, uint16_t moving_cm, uint16_t still_cm,
                                   uint8_t energy = 5, uint8_t max_gate = 8)
{
    std::vector<uint8_t> p = { type, 0xAA, state, (uint8_t)moving_cm, (uint8_t)(moving_cm >> 8), 60,
                               (uint8_t)still_cm, (uint8_t)(still_cm >> 8), 40, 0, 0 };
    if (type == 0x01) {
        p.push_back(max_gate);
        p.push_back(max_gate);
        for (int i = 0; i < 2 * (max_gate + 1); i++) {
            p.push_back(energy);
        }
        p.push_back(0x7F);
        p.push_back(0x00);
    }
    p.push_back(0x55);
    p.push_back(0x00);
    std::vector<uint8_t> f = { 0xF4, 0xF3, 0xF2, 0xF1, (uint8_t)p.size(), 0x00 };
    f.insert(f.end(), p.begin(), p.end());
    f.insert(f.end(), { 0xF8, 0xF7, 0xF6, 0xF5 });
    return f;
}

static std::vector<uint8_t> cat(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static void reset_driver()
{
    sim_reset();
    s_has_target = false;
    s_target = {};
    s_parser = {};
    s_capture = nullptr;
    s_ack_queue = nullptr;
    s_cmd_mutex = nullptr;
    CHECK(ld2410_init(3, 4) == ESP_OK);
}

static bool feed(const std::vector<uint8_t> &bytes, ld2410_target_t *t)
{
    ld2410_feed(bytes.data(), bytes.size());
    return ld2410_get_target(t);
}

static void test_parser()
{
    ld2410_target_t t;

    test_begin("parser: basic frame, moving and still");
    reset_driver();
    CHECK(feed(report(0x02, 0x03, 120, 80), &t));
    CHECK(t.state == 3 && t.moving && t.still && t.moving_cm == 120 && t.still_cm == 80);

    test_begin("parser: frame split over several reads");
    reset_driver();
    const auto f = report(0x02, 0x01, 150, 0);
    ld2410_feed(f.data(), 3);
    CHECK(!ld2410_get_target(&t));
    ld2410_feed(f.data() + 3, 7);
    ld2410_feed(f.data() + 10, f.size() - 10);
    CHECK(ld2410_get_target(&t) && t.moving && !t.still && t.moving_cm == 150);

    test_begin("parser: a byte that breaks a header starts the next one");
    reset_driver();
    CHECK(feed(cat({ 0x00, 0xF4, 0xF3, 0xF4, 0xF4 }, report(0x02, 0x02, 0, 95)), &t));
    CHECK(!t.moving && t.still && t.still_cm == 95);

    test_begin("parser: LEN above 64 is dropped, the next frame is read");
    reset_driver();
    CHECK(!feed({ 0xF4, 0xF3, 0xF2, 0xF1, 0x41, 0x00 }, &t));
    CHECK(feed(report(0x02, 0x01, 50, 0), &t) && t.moving_cm == 50);

    test_begin("parser: bad footer, wrong tail, wrong type or head are dropped");
    reset_driver();
    auto bad = report(0x02, 0x01, 50, 0);
    bad.back() = 0x00; // footer
    CHECK(!feed(bad, &t));
    bad = report(0x02, 0x01, 50, 0);
    bad[bad.size() - 6] = 0x56; // tail 0x55 0x00
    CHECK(!feed(bad, &t));
    bad = report(0x02, 0x01, 50, 0);
    bad[bad.size() - 5] = 0x01; // check byte
    CHECK(!feed(bad, &t));
    bad = report(0x02, 0x01, 50, 0);
    bad[6] = 0x03; // type
    CHECK(!feed(bad, &t));
    bad = report(0x02, 0x01, 50, 0);
    bad[7] = 0xAB; // head
    CHECK(!feed(bad, &t));
    CHECK(feed(report(0x02, 0x01, 77, 0), &t) && t.moving_cm == 77);

    test_begin("parser: a frame with missing bytes, then frames are read again");
    reset_driver();
    auto cut = report(0x02, 0x01, 300, 0);
    cut.resize(cut.size() - 8);
    ld2410_feed(cut.data(), cut.size());
    CHECK(feed(cat(report(0x02, 0x01, 310, 0), report(0x02, 0x02, 0, 320)), &t));
    CHECK(t.still && t.still_cm == 320);

    test_begin("parser: payload shorter than 13 bytes is dropped");
    reset_driver();
    CHECK(!feed({ 0xF4, 0xF3, 0xF2, 0xF1, 0x04, 0x00, 0x02, 0xAA, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5 }, &t));

    test_begin("parser: states 4-6 (noise detection) are never presence");
    for (uint8_t state = 4; state <= 6; state++) {
        reset_driver();
        CHECK(feed(report(0x02, state, 100, 100), &t));
        CHECK(t.state == state && !t.moving && !t.still);
    }

    test_begin("parser: engineering energies are counted while capturing, above 100 as 100");
    reset_driver();
    static ld2410_capture_t capture;
    ld2410_capture_start(&capture);
    CHECK(feed(report(0x01, 0x00, 0, 0, 30), &t));
    CHECK(feed(report(0x01, 0x00, 0, 0, 200), &t));
    CHECK(feed(report(0x02, 0x00, 0, 0), &t)); // basic report: not counted
    ld2410_capture_stop();
    CHECK(feed(report(0x01, 0x00, 0, 0, 30), &t)); // after stop: not counted
    CHECK(capture.frames == 2);
    CHECK(capture.moving[0][30] == 1 && capture.moving[8][30] == 1 && capture.still[4][30] == 1);
    CHECK(capture.moving[3][100] == 1 && capture.still[8][100] == 1);

    test_begin("parser: engineering data with more than 9 gates is not counted");
    reset_driver();
    ld2410_capture_start(&capture);
    CHECK(feed(report(0x01, 0x01, 100, 0, 30, 9), &t) && t.moving_cm == 100); // the target data is still used
    ld2410_capture_stop();
    CHECK(capture.frames == 0);
}

static void test_commands()
{
    test_begin("command: ACK with a return value");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        radar.no_one_duration = 60;
        radar.max_moving_gate = 6;
        CHECK(ld2410_config_begin() == ESP_OK);
        ld2410_config_t cfg;
        CHECK(ld2410_read_config(&cfg) == ESP_OK);
        CHECK(cfg.no_one_duration_s == 60 && cfg.max_moving_gate == 6 && cfg.max_still_gate == 8);
        CHECK(cfg.thresholds.moving[0] == 50 && cfg.thresholds.still[8] == 20);
        CHECK(ld2410_config_end() == ESP_OK);
        CHECK(radar.command_list() == "00FF 0061 00FE");
        CHECK(sim_ms() < 50);
    }

    test_begin("command: late ACK of the previous command is skipped");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        radar.answer_late = { 0x00A0 };
        CHECK(ld2410_config_begin() == ESP_OK);
        ld2410_version_t v;
        CHECK(ld2410_read_firmware_version(&v) == ESP_ERR_TIMEOUT);
        ld2410_config_t cfg;
        CHECK(ld2410_read_config(&cfg) == ESP_OK); // the late 0x00A0 ACK arrives first
        CHECK(cfg.max_moving_gate == 8);
        CHECK(sim_log_contains("No ACK for command 0x00A0"));
    }

    test_begin("command: refused, not answered");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        radar.refuse = { 0x0062 };
        radar.ignore = { 0x0063 };
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(ld2410_set_engineering_mode(true) == ESP_FAIL);
        CHECK(sim_log_contains("Command 0x0062 rejected, status 1"));
        const int64_t start = sim_ms();
        CHECK(ld2410_set_engineering_mode(false) == ESP_ERR_TIMEOUT);
        CHECK(sim_ms() - start >= 1000 && sim_ms() - start < 1010);
    }

    test_begin("command: configuration mode entered and left in up to 3 tries");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        radar.miss_next = 2;
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(radar.count(0x00FF) == 1 && sim_ms() >= 2000);
        radar.miss_next = 2;
        CHECK(ld2410_config_end() == ESP_OK);
        CHECK(radar.count(0x00FE) == 1 && !radar.config_mode);
        radar.miss_next = 3;
        const int64_t start = sim_ms();
        CHECK(ld2410_config_begin() == ESP_ERR_TIMEOUT);
        CHECK(sim_ms() - start >= 3000);
    }

    test_begin("command: restart without ACK is not an error, a refusal is");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(ld2410_restart() == ESP_OK); // with ACK
        sim_advance(1000);
        CHECK(radar.restarts == 1);
        radar.restart_without_ack = true;
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(ld2410_restart() == ESP_OK);
        CHECK(radar.restarts == 2 && !sim_log_contains("No ACK for command 0x00A3"));
        sim_advance(1000);
        radar.refuse = { 0x00A3 };
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(ld2410_restart() == ESP_FAIL);
        CHECK(radar.restarts == 2);
    }

    test_begin("command: Bluetooth state from the MAC address query, resolution");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        CHECK(ld2410_config_begin() == ESP_OK);
        bool on = false;
        uint8_t mac[6];
        CHECK(ld2410_read_bluetooth(&on, mac) == ESP_OK && on && mac[0] == 0x8F && mac[5] == 0x65);
        radar.bluetooth = false; // placeholder address 08:05:04:03:02:01
        CHECK(ld2410_read_bluetooth(&on, mac) == ESP_OK && !on && mac[0] == 0 && mac[5] == 0);
        uint16_t gate_cm = 0;
        CHECK(ld2410_read_gate_length(&gate_cm) == ESP_OK && gate_cm == 75);
        radar.resolution = 1;
        CHECK(ld2410_read_gate_length(&gate_cm) == ESP_OK && gate_cm == 20);
        radar.has_resolution = false;
        CHECK(ld2410_read_gate_length(&gate_cm) == ESP_FAIL);
        ld2410_version_t v;
        CHECK(ld2410_read_firmware_version(&v) == ESP_OK);
        char text[24];
        ld2410_format_version(&v, text, sizeof(text));
        CHECK(strcmp(text, "V2.44.25070917") == 0);
    }

    test_begin("command: values checked before sending");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        CHECK(ld2410_set_max_gates_and_duration(1, 8, 5) == ESP_ERR_INVALID_ARG);
        CHECK(ld2410_set_gate_thresholds(9, 50, 50) == ESP_ERR_INVALID_ARG);
        CHECK(ld2410_set_gate_thresholds(3, 101, 50) == ESP_ERR_INVALID_ARG);
        CHECK(ld2410_set_gate_length(50) == ESP_ERR_INVALID_ARG);
        CHECK(radar.commands.empty());
    }

    test_begin("command: gate thresholds and farthest gates are written as words");
    reset_driver();
    {
        RadarSim radar;
        radar.attach();
        CHECK(ld2410_config_begin() == ESP_OK);
        CHECK(ld2410_set_gate_thresholds(3, 35, 70) == ESP_OK);
        CHECK(ld2410_set_gate_thresholds(1, 60, 90) == ESP_OK);
        CHECK(radar.moving[3] == 35 && radar.still[3] == 70 && radar.moving[1] == 60 && radar.still[1] == 0);
        CHECK(ld2410_set_max_gates_and_duration(5, 4, 300) == ESP_OK);
        CHECK(radar.max_moving_gate == 5 && radar.max_still_gate == 4 && radar.no_one_duration == 300);
    }
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("ld2410:\n");
    test_parser();
    test_commands();
    return test_end();
}
