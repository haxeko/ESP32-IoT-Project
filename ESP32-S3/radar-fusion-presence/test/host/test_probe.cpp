// Host test of the radar probe channel task (radar_probe/main/probe_radar.cpp)
// against simulated radars (sim_probe.cpp): detection, capability queries,
// baud rate search, console jobs, silence, frame timestamps.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "sim_probe.h"

#include "probe_radar.cpp"

// Runs the task of channel 0 (UART1) until end_ms
static void run_channel(SimRadar radar, int64_t end_ms, uint32_t probe_baud = 256000)
{
    sim_reset(end_ms);
    g_sim.port[1].radar = radar;
    probe_radar_want_engineering(true);
    probe_radar_set_frames(true);
    CHECK(probe_radar_start(0, 1, 17, 18, probe_baud) == ESP_OK);
    CHECK(g_sim.tasks.size() == 1);
    try {
        g_sim.tasks[0].fn(g_sim.tasks[0].arg);
    } catch (SimEnd &) {
    }
}

// Fields of a record: "@D,1,7,123456,F4F3...*5A" -> {"D", "1", "7", "123456", "F4F3..."}
static std::vector<std::string> fields(const std::string &record)
{
    std::vector<std::string> out;
    std::string body = record.substr(1, record.rfind('*') - 1);
    size_t start = 0;
    for (;;) {
        const size_t comma = body.find(',', start);
        out.push_back(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            return out;
        }
        start = comma + 1;
    }
}

static bool checksums_ok()
{
    for (const std::string &r : g_sim.records) {
        const size_t star = r.rfind('*');
        if (r.empty() || r[0] != '@' || star == std::string::npos) {
            return false;
        }
        uint8_t c = 0;
        for (size_t i = 1; i < star; i++) {
            c ^= (uint8_t)r[i];
        }
        if (strtoul(r.substr(star + 1).c_str(), nullptr, 16) != c) {
            return false;
        }
    }
    return true;
}

static int64_t first_time_of(const char *needle)
{
    const std::string *r = sim_find(needle);
    if (!r) {
        return -1;
    }
    const std::vector<std::string> f = fields(*r);
    // @E,<t>,...  @I,<ch>,<t>,...
    return strtoll(f[f[0] == "E" ? 1 : 2].c_str(), nullptr, 10);
}

static void test_ld2410_boot()
{
    printf(" ld2410 at boot\n");
    SimRadar radar;
    radar.kind = SimRadar::LD2410;
    run_channel(radar, 12000);
    CHECK(checksums_ok());
    CHECK(sim_find("@I,1,") && sim_find(",radar,LD2410*"));
    CHECK(sim_find(",version,V2.44.24073110 (type 1)"));
    CHECK(sim_find(",bluetooth,off"));
    CHECK(sim_find(",params,max gate moving 8 still 8, no-one 5 s, thresholds moving 50 50 40 30 20 15 15 15 15 "
                   "still 0 0 40 40 30 30 20 20 20"));
    CHECK(sim_find(",resolution,75 cm per gate"));
    CHECK(sim_find(",noise_detection,idle"));
    CHECK(sim_find("job info done: 6 accepted, 0 refused, 0 unanswered"));
    CHECK(sim_find("job engineering_on done: 1 accepted, 0 refused, 0 unanswered"));
    const std::vector<uint16_t> expected = { 0x00FF, 0x00A0, 0x00A5, 0x0061, 0x00AB, 0x00AE, 0x001B, 0x00FE,
                                             0x00FF, 0x0062, 0x00FE };
    CHECK(g_sim.port[1].radar.commands == expected);

    // Reports: basic before the engineering job, engineering after it. Each
    // timestamp is the end of the report on the wire, computed from the byte
    // times of the UART event.
    const int64_t eng_done = first_time_of("job engineering_on done");
    int basic = 0, engineering = 0;
    std::vector<int64_t> stamps;
    uint32_t last_seq = 0;
    for (const std::string &r : g_sim.records) {
        if (r.compare(0, 3, "@D,") != 0) {
            continue;
        }
        const std::vector<std::string> f = fields(r);
        const int64_t t = strtoll(f[3].c_str(), nullptr, 10);
        const uint32_t seq = (uint32_t)strtoul(f[2].c_str(), nullptr, 10);
        CHECK(stamps.empty() || seq > last_seq);
        last_seq = seq;
        stamps.push_back(t);
        if (f[4].compare(0, 16, "F4F3F2F1230001AA") == 0) {
            engineering++;
            CHECK(t > eng_done - 100000);
        } else if (f[4].compare(0, 16, "F4F3F2F10D0002AA") == 0) {
            basic++;
        }
    }
    CHECK(stamps.size() == g_sim.report_ends.size());
    for (size_t i = 0; i < stamps.size() && i < g_sim.report_ends.size(); i++) {
        CHECK(llabs(stamps[i] - g_sim.report_ends[i]) <= 2);
    }
    CHECK(basic >= 1);        // before the engineering job
    CHECK(engineering >= 50); // the last seconds
    CHECK(sim_count("radar silent") == 0);

    // The records as capture.py stores them, for the decode.py check of run.sh
    if (const char *path = getenv("PROBE_TEST_RLOG")) {
        FILE *f = fopen(path, "w");
        CHECK(f != nullptr);
        for (size_t i = 0; f && i < g_sim.records.size(); i++) {
            fprintf(f, "%zu\t%s\n", 1700000000000 + i, g_sim.records[i].c_str());
        }
        if (f) {
            fclose(f);
        }
    }
}

static void test_ld2450_other_baud()
{
    printf(" ld2450 at 115200 baud\n");
    SimRadar radar;
    radar.kind = SimRadar::LD2450;
    radar.baud = 115200;
    run_channel(radar, 20000);
    CHECK(checksums_ok());
    CHECK(sim_find("baud scan start"));
    CHECK(sim_find("baud scan: radar at 115200"));
    CHECK(sim_find(",radar,LD2450*"));
    CHECK(sim_find(",version,V2.04.23101915 (type 0)"));
    CHECK(sim_find(",tracking,multi target"));
    CHECK(sim_find(",bluetooth,on, MAC 8F:27:2E:B8:0F:65"));
    CHECK(sim_find("job info done: 4 accepted, 0 refused, 0 unanswered"));
    CHECK(!sim_find("engineering_on"));
    CHECK(sim_find("@D,1,") && sim_find("AAFF03000E03B18610004001"));
}

static void test_old_ld2410()
{
    printf(" ld2410 without the newer commands\n");
    SimRadar radar;
    radar.kind = SimRadar::LD2410;
    radar.old_firmware = true;
    run_channel(radar, 8000);
    CHECK(sim_find("job info done: 4 accepted, 2 refused, 0 unanswered"));
    CHECK(sim_find("command 0x00AE refused (status 1)"));
}

static void test_no_radar()
{
    printf(" no radar\n");
    SimRadar radar; // NONE
    run_channel(radar, 30000);
    CHECK(sim_find("baud scan: no radar answered"));
    CHECK(sim_find("job info failed: no configuration mode"));
    CHECK(sim_count("@D,") == 0);
    // The scan ends on the configured rate
    const std::string *c = nullptr;
    for (const std::string &r : g_sim.records) {
        if (r.compare(0, 3, "@C,") == 0) {
            c = &r;
        }
    }
    CHECK(c && c->find("baud=256000") != std::string::npos);
}

static void test_console_jobs()
{
    printf(" console jobs\n");
    SimRadar radar;
    radar.kind = SimRadar::LD2410;
    sim_reset(30000);
    // At 10 s: "eng off"; at 14 s: "restart"; at 20 s: "setbaud 1 115200"
    g_sim.at = {
        { 10000000, [] {
             probe_radar_want_engineering(false);
             CHECK(probe_radar_job(0, PROBE_JOB_ENGINEERING, 0, nullptr, 0));
         } },
        { 14000000, [] { CHECK(probe_radar_job(0, PROBE_JOB_RESTART, 0, nullptr, 0)); } },
        { 20000000, [] { CHECK(probe_radar_job(0, PROBE_JOB_SET_BAUD, 115200, nullptr, 0)); } },
    };
    g_sim.port[1].radar = radar;
    probe_radar_want_engineering(true);
    CHECK(probe_radar_start(0, 1, 17, 18, 256000) == ESP_OK);
    try {
        g_sim.tasks[0].fn(g_sim.tasks[0].arg);
    } catch (SimEnd &) {
    }
    CHECK(sim_find("job engineering_off done: 1 accepted"));
    CHECK(sim_find("job restart done: 1 accepted"));
    CHECK(sim_find("job set_baud done: 2 accepted"));
    if (getenv("PROBE_TEST_DUMP")) {
        for (const std::string &r : g_sim.records) {
            if (r.compare(0, 3, "@D,") != 0 && r.compare(0, 3, "@H,") != 0) {
                printf("    %s\n", r.c_str());
            }
        }
    }
    CHECK(g_sim.port[1].radar.baud == 115200);
    CHECK(g_sim.port[1].baud == 115200);
    // Basic reports at the end: engineering mode off, not set again after the restarts
    const std::string *last = nullptr;
    for (const std::string &r : g_sim.records) {
        if (r.compare(0, 3, "@D,") == 0) {
            last = &r;
        }
    }
    CHECK(last && last->find("F4F3F2F10D0002AA") != std::string::npos);
    const int64_t t_last = last ? strtoll(fields(*last)[3].c_str(), nullptr, 10) : 0;
    CHECK(t_last > 29000000);
    CHECK(sim_count("radar silent") == 0);
}

static void test_silence()
{
    printf(" radar unplugged and back\n");
    SimRadar radar;
    radar.kind = SimRadar::LD2410;
    sim_reset(20000);
    g_sim.port[1].radar = radar;
    probe_radar_want_engineering(true);
    g_sim.at = {
        { 8000000, [] { g_sim.port[1].radar.kind = SimRadar::NONE; } },
        { 15000000, [] {
             // Powered on again (pwr 1 on): the radar starts without engineering mode
             g_sim.port[1].radar.kind = SimRadar::LD2410;
             g_sim.port[1].radar.engineering = false;
             g_sim.port[1].radar.config = false;
             g_sim.port[1].radar.next_report_us = g_sim.now_us + 20000;
             CHECK(probe_radar_job(0, PROBE_JOB_POWERED, 0, nullptr, 0));
         } },
    };
    CHECK(probe_radar_start(0, 1, 17, 18, 256000) == ESP_OK);
    try {
        g_sim.tasks[0].fn(g_sim.tasks[0].arg);
    } catch (SimEnd &) {
    }
    const int64_t silent = first_time_of("radar silent");
    CHECK(silent > 11000000 && silent < 11300000);
    CHECK(sim_find("radar back after"));
    // Engineering mode set again about 2 s after the power returned
    const std::string *last = nullptr;
    for (const std::string &r : g_sim.records) {
        if (r.compare(0, 3, "@D,") == 0) {
            last = &r;
        }
    }
    CHECK(last && last->find("F4F3F2F1230001AA") != std::string::npos);
}

int main()
{
    printf("probe:\n");
    CHECK(probe_out_init() == ESP_OK); // records go to g_sim.records (stubs/freertos/ringbuf.h)
    test_ld2410_boot();
    test_ld2450_other_baud();
    test_old_ld2410();
    test_no_radar();
    test_console_jobs();
    test_silence();
    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
