// The presence engine on the device (esp_presence.cpp) against the simulated radar, through the LD2410 driver: the
// learning saved in NVS and loaded at the next boot, a damaged blob left alone, the occupancy handed to the
// presence task, a silent radar (no "empty"), reports without energies (engineering mode wanted), the commands,
// the LED and the "Learn empty room" select.
#include "sim.h"

#include "esp_presence.cpp"

#include <stdlib.h>

#include <vector>

#include "radar_sim.h"

static std::vector<int> g_led;
void status_led_set_learning(status_led_learning_t learning)
{
    g_led.push_back(learning);
}

static std::vector<int> g_view;
static void view_learn_mode(uint8_t mode)
{
    g_view.push_back(mode);
}

// Empty room: noisy near gates, quiet far gates; someone at a gate on top
static int g_person_gate = -1;
static uint8_t g_person_moving;
static uint8_t g_person_still;
static uint32_t g_seed = 1;

static int noise(int median, int spread)
{
    g_seed = g_seed * 1103515245u + 12345u;
    const int r = (int)((g_seed >> 16) % (2 * spread + 1)) - spread;
    return std::clamp(median + r, 0, 100);
}

static void room_energies(int64_t ms, uint8_t *m, uint8_t *s)
{
    static const int moving[9] = { 28, 22, 12, 8, 6, 4, 3, 3, 2 };
    static const int still[9] = { 35, 25, 10, 6, 4, 3, 2, 2, 2 };
    for (int g = 0; g < 9; g++) {
        m[g] = (uint8_t)noise(moving[g], 2 + moving[g] / 8);
        s[g] = (uint8_t)noise(still[g], 2 + still[g] / 10);
    }
    if (g_person_gate >= 0) {
        m[g_person_gate] = std::max(m[g_person_gate], g_person_moving);
        s[g_person_gate] = std::max(s[g_person_gate], g_person_still);
    }
}

// New boot of the ESP32 (the NVS stays unless clear_nvs) with a radar in engineering mode
static void boot(RadarSim &radar, bool clear_nvs)
{
    sim_reset(clear_nvs);
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.bluetooth = false;
    radar.stored_bluetooth = false;
    radar.attach();
    radar.engineering = true;
    radar.energies = room_energies;
    g_person_gate = -1;
    g_led.clear();
    g_view.clear();
    s_started = false;
    s_dropped = 0;
    s_hold_time_s = 0;
    s_busy = false;
    s_range_pending = false;
    s_occupied = false;
    s_needs_engineering = false;
    s_silent_ms = 0;
    esp_presence_set_learn_view(view_learn_mode);
    CHECK(esp_presence_start() == ESP_OK);
    radar_settings_t settings = {};
    settings.gate_cm = 75;
    settings.config.max_moving_gate = 8;
    settings.config.max_still_gate = 8;
    esp_presence_configure(&settings);
}

static void run_engine(int64_t until_ms)
{
    g_sim.end_ms = until_ms;
    try {
        engine_task(nullptr);
    } catch (SimEnd &) {
    }
}

static bool stored_profile(pe_profile_t *out)
{
    auto ns = g_sim.nvs.find("esp_presence");
    if (ns == g_sim.nvs.end()) {
        return false;
    }
    auto it = ns->second.find("profile");
    if (it == ns->second.end() || it->second.size() != sizeof(pe_profile_t)) {
        return false;
    }
    memcpy(out, it->second.data(), sizeof(*out));
    return true;
}

static bool led_has(status_led_learning_t learning)
{
    return std::find(g_led.begin(), g_led.end(), (int)learning) != g_led.end();
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("ESP32 presence engine on the device:\n");

    test_begin("first boot: built-in thresholds, learning needed; LEARN_EMPTY learns, saves in NVS, shows it");
    {
        RadarSim radar;
        boot(radar, true);
        CHECK(sim_log_contains("No learned thresholds yet: built-in per-gate thresholds in use"));
        sim_at(2000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        run_engine(2000 + 20000 + 60000 + 1000);
        pe_profile_t p;
        CHECK(stored_profile(&p) && pe_profile_valid(&p) && p.learned && p.sequence == 1 && p.gate_cm == 75);
        CHECK(pe_profile_learned(&s_engine));
        CHECK(sim_log_contains("LEARN_EMPTY: leave the room, recording the empty room in 20 s for 60 s"));
        CHECK(sim_log_contains("Learning: OK, new thresholds saved"));
        CHECK(sim_log_contains("Learned thresholds #1 saved in NVS"));
        CHECK(sim_log_contains("  gate 8 6-6.75 m"));
        CHECK(led_has(STATUS_LED_LEARNING_WAITING) && led_has(STATUS_LED_LEARNING_RECORDING) &&
              g_led.back() == STATUS_LED_LEARNING_DONE);
        CHECK(g_view.size() == 2 && g_view[0] == RADAR_LEARN_ON && g_view[1] == RADAR_LEARN_OFF);
    }

    test_begin("next boot: the learned thresholds are loaded and used; a second learning gets #2");
    {
        RadarSim radar;
        boot(radar, false);
        CHECK(sim_log_contains("Learned thresholds #1 loaded (75 cm gates"));
        CHECK(pe_profile_learned(&s_engine));
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        run_engine(1000 + 80000 + 1000);
        pe_profile_t p;
        CHECK(stored_profile(&p) && p.sequence == 2);
    }

    test_begin("damaged blob in NVS: built-in thresholds, the blob is left as it is");
    {
        RadarSim radar;
        auto &blob = g_sim.nvs["esp_presence"]["profile"];
        blob[40] ^= 0x55;
        const std::vector<uint8_t> damaged = blob;
        boot(radar, false);
        CHECK(sim_log_contains("Stored thresholds damaged or of another version"));
        CHECK(!pe_profile_learned(&s_engine));
        run_engine(5000);
        CHECK(g_sim.nvs["esp_presence"]["profile"] == damaged);
    }

    test_begin("presence: walking in occupies, sitting still keeps it, empty a hold time after leaving");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        run_engine(83000);
        CHECK(pe_profile_learned(&s_engine));
        esp_presence_set_hold_time(60);
        run_engine(90000);
        CHECK(!esp_presence_occupied());
        sim_at(90000, []() {
            g_person_gate = 4;
            g_person_moving = 70;
            g_person_still = 40;
        });
        run_engine(91000);
        CHECK(esp_presence_occupied() && sim_log_contains("Present: moving gate 4 energy"));
        sim_at(95000, []() {
            g_person_moving = 0;
            g_person_still = (uint8_t)(pe_hold_threshold(&s_engine, PE_STILL, 4) + 4);
        });
        run_engine(400000);
        CHECK(esp_presence_occupied());
        sim_at(400000, []() { g_person_gate = -1; });
        run_engine(455000);
        CHECK(!esp_presence_occupied() && s_engine.presence == PE_PRESENCE_PRESENT); // counting down
        run_engine(462000);
        CHECK(s_engine.presence == PE_PRESENCE_WAITING && sim_log_contains("Empty: no hold signal for 60 s"));
    }

    test_begin("radar silent while present: stays occupied, silence published for the recovery");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        run_engine(83000);
        CHECK(pe_profile_learned(&s_engine));
        esp_presence_set_hold_time(60);
        sim_at(84000, []() {
            g_person_gate = 2;
            g_person_moving = 90;
            g_person_still = 40;
        });
        run_engine(86000);
        CHECK(esp_presence_occupied());
        sim_at(86000, [&radar]() {
            g_person_gate = -1;
            radar.report_silent = true;
        });
        run_engine(93000);
        CHECK(esp_presence_occupied() && esp_presence_silent_ms() >= 6000);
        CHECK(sim_log_contains("Sensor fault: no data from the radar. Presence stays as it is (present)"));
        run_engine(383000);
        CHECK(esp_presence_occupied());
        sim_at(383000, [&radar]() { radar.report_silent = false; });
        run_engine(384000);
        CHECK(esp_presence_silent_ms() == 0 && sim_log_contains("Radar energies received"));
        run_engine(438000);
        CHECK(s_engine.presence == PE_PRESENCE_PRESENT);
        run_engine(445000);
        CHECK(s_engine.presence == PE_PRESENCE_WAITING);
    }

    test_begin("reports without energies: engineering mode wanted, sensor fault after 2 s");
    {
        RadarSim radar;
        boot(radar, true);
        radar.engineering = false;
        run_engine(1500);
        CHECK(esp_presence_needs_engineering());
        run_engine(3000);
        CHECK(s_engine.sensor == PE_SENSOR_NO_ENERGY && esp_presence_silent_ms() == 0);
        radar.engineering = true;
        run_engine(4000);
        CHECK(!esp_presence_needs_engineering() && s_engine.sensor == PE_SENSOR_OK);
    }

    test_begin("busy while configuring: labelled as configuration, not as a radar fault");
    {
        RadarSim radar;
        boot(radar, true);
        run_engine(2000);
        sim_at(2000, [&radar]() {
            radar.report_silent = true;
            esp_presence_set_radar_busy(true);
        });
        run_engine(5000);
        CHECK(s_engine.sensor == PE_SENSOR_CONFIGURING);
        sim_at(5000, [&radar]() {
            radar.report_silent = false;
            esp_presence_set_radar_busy(false);
        });
        run_engine(6000);
        CHECK(s_engine.sensor == PE_SENSOR_OK);
    }

    test_begin("commands: STATUS, DIAG, PROFILE print; PROFILE_RESET erases the learned thresholds");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        run_engine(83000);
        CHECK(pe_profile_learned(&s_engine));
        sim_at(84000, []() {
            esp_presence_command(ESP_PRESENCE_PRINT_STATUS);
            esp_presence_command(ESP_PRESENCE_PRINT_DIAG);
            esp_presence_command(ESP_PRESENCE_PRINT_PROFILE);
        });
        run_engine(85000);
        CHECK(sim_log_contains("STATUS presence: empty, waiting for a strong signal"));
        CHECK(sim_log_contains("STATUS sensor: OK"));
        CHECK(sim_log_contains("Thresholds in use: learned #1 (75 cm gates)"));
        CHECK(sim_log_contains("DIAG gate distance"));
        CHECK(sim_log_count("DIAG    ") >= 9);
        CHECK(sim_log_contains("PROFILE learned #1"));
        g_led.clear();
        g_view.clear();
        sim_at(86000, []() { esp_presence_command(ESP_PRESENCE_PROFILE_RESET); });
        run_engine(87000);
        pe_profile_t p;
        CHECK(!stored_profile(&p) && !pe_profile_learned(&s_engine));
        CHECK(sim_log_contains("PROFILE_RESET: learned thresholds erased"));
        CHECK(g_led.back() == STATUS_LED_LEARNING_FACTORY);
        CHECK(g_view.size() == 2 && g_view[0] == RADAR_LEARN_FACTORY && g_view[1] == RADAR_LEARN_OFF);
        // DIAG stream: one table per second
        sim_at(88000, []() { esp_presence_command(ESP_PRESENCE_DIAG_STREAM_ON); });
        run_engine(93500);
        const int tables = sim_log_count("DIAG gate distance");
        CHECK(tables >= 5 && tables <= 7);
        sim_at(93500, []() { esp_presence_command(ESP_PRESENCE_DIAG_STREAM_OFF); });
        run_engine(97000);
        CHECK(sim_log_count("DIAG gate distance") == tables);
    }

    test_begin("learning cancelled: nothing stored, LED back to normal, select Off");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        sim_at(30000, []() { esp_presence_command(ESP_PRESENCE_LEARN_CANCEL); });
        run_engine(32000);
        pe_profile_t p;
        CHECK(!stored_profile(&p) && sim_log_contains("Learning cancelled: the old thresholds stay"));
        CHECK(g_led.back() == STATUS_LED_LEARNING_NONE && g_view.back() == RADAR_LEARN_OFF);
    }

    test_begin("learning with someone moving everywhere: fails, the gates are listed, old thresholds stay");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_command(ESP_PRESENCE_LEARN); });
        for (int t = 21000; t < 81000; t += 1000) {
            sim_at(t, [t]() {
                g_person_gate = (t / 1000) % 9;
                g_person_moving = 99;
                g_person_still = 99;
            });
        }
        run_engine(83000);
        pe_profile_t p;
        CHECK(!stored_profile(&p) && !pe_profile_learned(&s_engine));
        CHECK(sim_log_contains("Learning failed: too many gates too noisy") ||
              sim_log_contains("Learning failed: no gate in range can trigger"));
        CHECK(sim_log_contains("TOO NOISY") && g_led.back() == STATUS_LED_LEARNING_FAILED);
    }

    test_begin("damaged frames on the line are counted, not used");
    {
        RadarSim radar;
        boot(radar, true);
        run_engine(1000);
        sim_at(1000, []() {
            // A report with a wrong tail, then garbage
            static const uint8_t bad[] = { 0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x01, 0x10, 0x00, 0x30,
                                           0x10, 0x00, 0x30, 0x10, 0x00, 0x00, 0x00, 0xF8, 0xF7, 0xF6, 0xF5 };
            ld2410_feed(bad, sizeof(bad));
        });
        run_engine(2000);
        CHECK(s_engine.frames_bad >= 1 && s_engine.sensor == PE_SENSOR_OK);
    }

    return test_end();
}
