// One configuration session with the radar (main/radar_settings.cpp) against the simulated radar, and the labels.
#include "sim.h"

#include <string.h>

#include "radar_settings.h"
#include "radar_sim.h"

static radar_settings_t session(RadarSim &radar, radar_settings_request_t want, esp_err_t expect)
{
    radar_settings_t out = {};
    const esp_err_t err = radar_settings_session(&want, &out);
    if (err != expect) {
        printf("    session returned 0x%x instead of 0x%x, commands %s\n", err, expect, radar.command_list().c_str());
    }
    CHECK(err == expect);
    return out;
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
}

static void test_labels()
{
    test_begin("labels: distances, durations, MAC, custom range");
    char buf[48];
    radar_format_distance(150, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.5 m") == 0);
    radar_format_distance(225, buf, sizeof(buf));
    CHECK(strcmp(buf, "2.25 m") == 0);
    radar_format_distance(600, buf, sizeof(buf));
    CHECK(strcmp(buf, "6 m") == 0);
    radar_format_distance(40, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.4 m") == 0);
    radar_format_distance(160, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.6 m") == 0);
    radar_format_distance(525, buf, sizeof(buf));
    CHECK(strcmp(buf, "5.25 m") == 0);
    radar_format_duration(5, buf, sizeof(buf));
    CHECK(strcmp(buf, "5 s") == 0);
    radar_format_duration(60, buf, sizeof(buf));
    CHECK(strcmp(buf, "1 min") == 0);
    radar_format_duration(90, buf, sizeof(buf));
    CHECK(strcmp(buf, "1 min 30 s") == 0);
    const uint8_t mac[6] = { 0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65 };
    radar_format_mac(mac, buf, sizeof(buf));
    CHECK(strcmp(buf, "8F:27:2E:B8:0F:65") == 0);
    radar_format_range(6, 4, 75, buf, sizeof(buf));
    CHECK(strcmp(buf, "Moving 4.5 m, still 3 m") == 0);
    radar_format_range(8, 5, 20, buf, sizeof(buf));
    CHECK(strcmp(buf, "Moving 1.6 m, still 1 m") == 0);
}

static void test_sessions()
{
    test_begin("session: read only, no restart, no write");
    {
        RadarSim radar;
        start(radar);
        radar.no_one_duration = 60;
        const radar_settings_t s = session(radar, {}, ESP_OK);
        CHECK(radar.command_list() == "00FF 00AB 00A5 0061 00FE");
        CHECK(s.gate_cm == 75 && s.resolution_settable && s.bluetooth_known && s.bluetooth_on);
        CHECK(s.bt_mac[0] == 0x8F && s.config.no_one_duration_s == 60 && s.config.max_moving_gate == 8);
        CHECK(radar.restarts == 0 && !radar.config_mode);
    }

    test_begin("session: resolution change, one restart, read back");
    {
        RadarSim radar;
        start(radar);
        radar_settings_request_t want;
        want.gate_cm = 20;
        const radar_settings_t s = session(radar, want, ESP_OK);
        CHECK(radar.restarts == 1 && radar.resolution == 1 && s.gate_cm == 20);
        CHECK(radar.command_list() == "00FF 00AB 00A5 00AA 00A3 00FF 00AB 00A5 0061 00FE");
        CHECK(!radar.config_mode);
    }

    test_begin("session: resolution and Bluetooth applied by the same restart");
    {
        RadarSim radar;
        start(radar);
        radar_settings_request_t want;
        want.gate_cm = 20;
        want.bluetooth = 0;
        const radar_settings_t s = session(radar, want, ESP_OK);
        CHECK(radar.restarts == 1 && radar.resolution == 1 && !radar.bluetooth);
        CHECK(s.gate_cm == 20 && s.bluetooth_known && !s.bluetooth_on);
    }

    test_begin("session: the radar restarts before its ACK");
    {
        RadarSim radar;
        start(radar);
        radar.restart_without_ack = true;
        radar_settings_request_t want;
        want.bluetooth = 0;
        const radar_settings_t s = session(radar, want, ESP_OK);
        CHECK(radar.restarts == 1 && !s.bluetooth_on);
    }

    test_begin("session: a step before the restart fails, stored values set back");
    {
        RadarSim radar;
        start(radar);
        radar.refuse = { 0x00A4 }; // Bluetooth refused after the resolution was stored
        radar_settings_request_t want;
        want.gate_cm = 20;
        want.bluetooth = 0;
        session(radar, want, ESP_FAIL);
        CHECK(radar.restarts == 0 && radar.stored_resolution == 0 && radar.resolution == 0);
        CHECK(radar.count(0x00AA) == 2); // set, then set back
        CHECK(!radar.config_mode);
    }

    test_begin("session: restart refused, stored values set back");
    {
        RadarSim radar;
        start(radar);
        radar.refuse = { 0x00A3 };
        radar_settings_request_t want;
        want.bluetooth = 0;
        session(radar, want, ESP_FAIL);
        CHECK(radar.restarts == 0 && radar.stored_bluetooth && radar.bluetooth);
        CHECK(sim_log_contains("The radar refused to restart"));
    }

    test_begin("session: the radar does not follow after its restart");
    {
        RadarSim radar;
        start(radar);
        radar.bluetooth_stuck = true;
        radar_settings_request_t want;
        want.bluetooth = 0;
        session(radar, want, ESP_ERR_INVALID_RESPONSE);
        CHECK(radar.restarts == 1 && sim_log_contains("The radar Bluetooth is still on after its restart"));
        CHECK(!radar.config_mode);
    }

    test_begin("session: fixed 0.75 m radar (no resolution query)");
    {
        RadarSim radar;
        start(radar);
        radar.has_resolution = false;
        radar_settings_t s = session(radar, {}, ESP_OK);
        CHECK(!s.resolution_settable && s.gate_cm == 75);
        radar_settings_request_t want;
        want.gate_cm = 20;
        s = session(radar, want, ESP_ERR_NOT_SUPPORTED);
        CHECK(radar.count(0x00AA) == 0 && radar.restarts == 0 && !radar.config_mode);
    }

    test_begin("session: radar without Bluetooth (query not answered, asked twice)");
    {
        RadarSim radar;
        start(radar);
        radar.has_bluetooth = false;
        radar.refuse_unsupported = false;
        radar_settings_t s = session(radar, {}, ESP_OK);
        CHECK(!s.bluetooth_known && !s.bluetooth_on && radar.count(0x00A5) == 2);
        radar_settings_request_t want;
        want.bluetooth = 1;
        s = session(radar, want, ESP_ERR_NOT_SUPPORTED);
        CHECK(radar.count(0x00A4) == 0 && radar.restarts == 0);
    }

    test_begin("session: Range written for moving and still, no-one duration kept, read back");
    {
        RadarSim radar;
        start(radar);
        radar.no_one_duration = 60;
        radar.max_still_gate = 4;
        radar_settings_request_t want;
        want.gate = 5;
        const radar_settings_t s = session(radar, want, ESP_OK);
        CHECK(radar.max_moving_gate == 5 && radar.max_still_gate == 5 && radar.no_one_duration == 60);
        CHECK(s.config.max_moving_gate == 5 && s.config.max_still_gate == 5 && s.config.no_one_duration_s == 60);
        CHECK(radar.command_list() == "00FF 00AB 00A5 0061 0060 0061 00FE");
    }

    test_begin("session: Range already held, nothing written");
    {
        RadarSim radar;
        start(radar);
        radar_settings_request_t want;
        want.gate = 8;
        session(radar, want, ESP_OK);
        CHECK(radar.count(0x0060) == 0);
    }

    test_begin("session: Range refused");
    {
        RadarSim radar;
        start(radar);
        radar.refuse = { 0x0060 };
        radar_settings_request_t want;
        want.gate = 3;
        session(radar, want, ESP_FAIL);
        CHECK(radar.max_moving_gate == 8 && !radar.config_mode);
    }

    test_begin("session: radar does not answer (wire to its RX pin)");
    {
        RadarSim radar;
        start(radar);
        radar.tx_wire_broken = true;
        session(radar, {}, ESP_ERR_TIMEOUT);
        CHECK(sim_ms() >= 3000);
    }

    test_begin("session: radar left in configuration mode (ESP32 reset during a session) sends reports again");
    {
        RadarSim radar;
        start(radar);
        radar.config_mode = true; // no reports
        sim_advance(500);
        const int64_t session_start = sim_ms();
        session(radar, {}, ESP_OK);
        sim_advance(500);
        ld2410_target_t t;
        CHECK(ld2410_get_target(&t) && t.timestamp_ms > session_start);
    }
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("radar_settings:\n");
    test_labels();
    test_sessions();
    return test_end();
}
