// The presence engine on the device (esp_presence.cpp) against the simulated radar, through the LD2410 driver: the
// radar's thresholds handed over by the radar_cfg task, the noise recorded while learning saved in NVS and loaded at
// the next boot, a damaged blob left alone, the occupancy handed to the presence task, a silent radar (no "empty"),
// reports without energies (engineering mode wanted), the radar busy, the commands and the logs.
#include "sim.h"

#include "esp_presence.cpp"

#include <stdlib.h>

#include <vector>

#include "radar_sim.h"

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

// What the learning hands over: `reports` reports of the empty room
static ld2410_capture_t g_capture;
static const ld2410_capture_t *room_capture(uint32_t reports)
{
    memset(&g_capture, 0, sizeof(g_capture));
    for (uint32_t i = 0; i < reports; i++) {
        uint8_t m[9];
        uint8_t s[9];
        room_energies(0, m, s);
        for (int g = 0; g < 9; g++) {
            g_capture.moving[g][m[g]]++;
            g_capture.still[g][s[g]]++;
        }
    }
    g_capture.frames = reports;
    return &g_capture;
}

static radar_settings_t settings_with(const pe_radar_thresholds_t &t, uint16_t gate_cm = 75)
{
    radar_settings_t settings = {};
    settings.gate_cm = gate_cm;
    settings.config.max_moving_gate = 8;
    settings.config.max_still_gate = 8;
    memcpy(settings.config.thresholds.moving, t.moving, sizeof(t.moving));
    memcpy(settings.config.thresholds.still, t.still, sizeof(t.still));
    return settings;
}

static pe_radar_thresholds_t factory()
{
    pe_radar_thresholds_t t;
    pe_factory_thresholds(&t);
    return t;
}

// New boot of the ESP32 (the NVS stays unless clear_nvs) with a radar in engineering mode; the radar_cfg task hands
// over the factory thresholds read from the radar
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
    s_started = false;
    s_dropped = 0;
    s_hold_time_s = 0;
    s_busy = false;
    s_config_pending = false;
    s_noise_pending = false;
    s_occupied = false;
    s_needs_engineering = false;
    s_silent_ms = 0;
    CHECK(esp_presence_start() == ESP_OK);
    const radar_settings_t settings = settings_with(factory());
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

static bool stored_noise(pe_noise_t *out)
{
    auto ns = g_sim.nvs.find("esp_presence");
    if (ns == g_sim.nvs.end()) {
        return false;
    }
    auto it = ns->second.find("noise");
    if (it == ns->second.end() || it->second.size() != sizeof(pe_noise_t)) {
        return false;
    }
    memcpy(out, it->second.data(), sizeof(*out));
    return true;
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("ESP32 presence engine on the device:\n");

    test_begin("first boot: no noise recorded, the radar's thresholds used as they are, learning advised");
    {
        RadarSim radar;
        boot(radar, true);
        CHECK(sim_log_contains("No noise of the empty room recorded yet: hold thresholds = radar thresholds"));
        run_engine(3000);
        CHECK(s_engine.th.from_radar && !s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 20);
        CHECK(sim_log_contains("Thresholds from the radar (75 cm gates): strong above the radar threshold, hold at "
                               "radar - 40 % (at most 10), at least the empty room noise + 2"));
        CHECK(sim_log_contains("  gate 8 6-6.75 m    moving: radar 15 hold 15 | still: radar 20 hold 20"));
        CHECK(sim_log_contains("  gate 0 0-0.75 m    moving: radar 50 hold 50 | still: no radar threshold"));
        CHECK(sim_log_contains("The radar holds its factory thresholds: learn the empty room"));
        CHECK(sim_log_contains("No noise of the empty room recorded: hold = radar threshold until the empty room is "
                               "learned"));
        CHECK(sim_log_count("Thresholds from the radar") == 1);
    }

    test_begin("learning: the noise saved in NVS and used (hold below the radar threshold), loaded at the next boot");
    {
        RadarSim radar;
        boot(radar, true);
        run_engine(2000);
        // The radar_cfg task: the noise from the learning, then the thresholds the radar stored
        sim_at(2000, []() {
            esp_presence_learned_noise(room_capture(300), 75);
            pe_radar_thresholds_t t = factory();
            t.moving[4] = 25;
            const radar_settings_t settings = settings_with(t);
            esp_presence_configure(&settings);
        });
        run_engine(5000);
        pe_noise_t n;
        CHECK(stored_noise(&n) && pe_noise_valid(&n) && n.frames == 300 && n.gate_cm == 75);
        CHECK(s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 12);
        CHECK(pe_trigger_threshold(&s_engine, PE_MOVING, 4) == 26 && pe_hold_threshold(&s_engine, PE_MOVING, 4) == 15);
        CHECK(sim_log_contains("Noise of the empty room recorded (300 reports) and saved"));
        CHECK(sim_log_contains("  gate 4 3-3.75 m    moving: radar 25 hold 15 (noise "));
        CHECK(sim_log_count("Thresholds from the radar") == 2); // boot, then once for both changes
        CHECK(sim_log_count("The radar holds its factory thresholds") == 1); // only at boot

        RadarSim again;
        boot(again, false);
        CHECK(sim_log_contains("Noise of the empty room loaded (300 reports, 75 cm gates)"));
        run_engine(3000);
        CHECK(s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 12);
    }

    test_begin("damaged noise in NVS: hold = radar threshold, the blob is left as it is");
    {
        RadarSim radar;
        auto &blob = g_sim.nvs["esp_presence"]["noise"];
        CHECK(blob.size() == sizeof(pe_noise_t));
        blob[20] ^= 0x55;
        const std::vector<uint8_t> damaged = blob;
        boot(radar, false);
        CHECK(sim_log_contains("Stored noise damaged or of another version (40 bytes)"));
        run_engine(3000);
        CHECK(!s_engine.has_noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 20);
        CHECK(g_sim.nvs["esp_presence"]["noise"] == damaged);
    }

    test_begin("noise recorded with 0.75 m gates while the radar uses 0.2 m: warned, hold = radar threshold");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() {
            esp_presence_learned_noise(room_capture(300), 75);
            const radar_settings_t settings = settings_with(factory(), 20);
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        CHECK(s_engine.has_noise && !s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 20);
        CHECK(sim_log_contains("The noise was recorded with 75 cm gates and the radar uses 20 cm gates"));
    }

    test_begin("noise above a radar threshold: warned (presence may start without anyone)");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() {
            esp_presence_learned_noise(room_capture(300), 75);
            pe_radar_thresholds_t t = factory();
            t.moving[0] = 25; // the noise of gate 0 is about 28
            const radar_settings_t settings = settings_with(t);
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        CHECK(sim_log_contains("Gate 0 moving: the empty room noise"));
        CHECK(sim_log_contains("is above the radar threshold 25, presence may start without anyone"));
        CHECK(pe_hold_threshold(&s_engine, PE_MOVING, 0) == 25);
    }

    test_begin("presence: walking in occupies, sitting still (above hold, not above the radar) keeps it, empty a "
               "hold time after leaving");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_learned_noise(room_capture(300), 75); });
        esp_presence_set_hold_time(60);
        run_engine(5000);
        CHECK(!esp_presence_occupied() && sim_log_contains("Hold time 60 s"));
        sim_at(5000, []() {
            g_person_gate = 4;
            g_person_moving = 70;
            g_person_still = 40;
        });
        run_engine(6000);
        CHECK(esp_presence_occupied() && sim_log_contains("Present: moving gate 4 energy"));
        CHECK(sim_log_contains("above the radar threshold 20 (3 of the last 4 reports strong)"));
        sim_at(9000, []() {
            g_person_moving = 0;
            g_person_still = (uint8_t)(pe_hold_threshold(&s_engine, PE_STILL, 4) + 4);
        });
        run_engine(300000);
        CHECK(esp_presence_occupied() && pe_hold_threshold(&s_engine, PE_STILL, 4) + 4 <= 30);
        sim_at(300000, []() { g_person_gate = -1; });
        run_engine(355000);
        CHECK(!esp_presence_occupied() && s_engine.presence == PE_PRESENCE_PRESENT); // counting down
        run_engine(362000);
        CHECK(s_engine.presence == PE_PRESENCE_WAITING && sim_log_contains("Empty: no hold signal for 60 s"));
    }

    test_begin("radar silent while present: stays occupied, silence published for the recovery");
    {
        RadarSim radar;
        boot(radar, true);
        esp_presence_set_hold_time(60);
        sim_at(2000, []() {
            g_person_gate = 2;
            g_person_moving = 90;
            g_person_still = 60;
        });
        run_engine(4000);
        CHECK(esp_presence_occupied());
        sim_at(4000, [&radar]() {
            g_person_gate = -1;
            radar.report_silent = true;
        });
        run_engine(11000);
        CHECK(esp_presence_occupied() && esp_presence_silent_ms() >= 6000);
        CHECK(sim_log_contains("Sensor fault: no data from the radar. Presence stays as it is (present)"));
        run_engine(300000);
        CHECK(esp_presence_occupied());
        sim_at(300000, [&radar]() { radar.report_silent = false; });
        run_engine(301000);
        CHECK(esp_presence_silent_ms() == 0 && sim_log_contains("Radar energies received"));
        run_engine(355000);
        CHECK(s_engine.presence == PE_PRESENCE_PRESENT);
        run_engine(362000);
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

    test_begin("radar busy (configuration or learning): labelled as such, not as a radar fault");
    {
        RadarSim radar;
        boot(radar, true);
        run_engine(2000);
        sim_at(2000, [&radar]() {
            radar.report_silent = true;
            esp_presence_set_radar_busy(true);
        });
        run_engine(5000);
        CHECK(s_engine.sensor == PE_SENSOR_BUSY);
        CHECK(sim_log_contains("Sensor fault: radar being configured or learning the room"));
        sim_at(5000, [&radar]() {
            radar.report_silent = false;
            esp_presence_set_radar_busy(false);
        });
        run_engine(6000);
        CHECK(s_engine.sensor == PE_SENSOR_OK);
    }

    test_begin("commands: STATUS, DIAG, THRESHOLDS print; DIAG stream every second");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { esp_presence_learned_noise(room_capture(300), 75); });
        run_engine(4000);
        sim_at(4000, []() {
            esp_presence_command(ESP_PRESENCE_PRINT_STATUS);
            esp_presence_command(ESP_PRESENCE_PRINT_DIAG);
            esp_presence_command(ESP_PRESENCE_PRINT_THRESHOLDS);
        });
        const int tables_before = sim_log_count("Thresholds from the radar");
        run_engine(5000);
        CHECK(sim_log_contains("STATUS presence: empty, waiting for a strong signal"));
        CHECK(sim_log_contains("STATUS sensor: OK"));
        CHECK(sim_log_contains("STATUS thresholds: from the radar (its factory thresholds: learn the empty room); "
                               "noise recorded (300 reports)"));
        CHECK(sim_log_contains("STATUS range: moving gates 0-8 (to 6.75 m), still gates 2-8 (to 6.75 m)"));
        CHECK(sim_log_contains("DIAG gate distance    | moving now filt peak hold radar | still now filt peak hold "
                               "radar"));
        CHECK(sim_log_count("DIAG    ") >= 9 && sim_log_contains("(not counted)"));
        CHECK(sim_log_count("Thresholds from the radar") == tables_before + 1);
        sim_at(6000, []() { esp_presence_command(ESP_PRESENCE_DIAG_STREAM_ON); });
        run_engine(11500);
        const int tables = sim_log_count("DIAG gate distance");
        CHECK(tables >= 6 && tables <= 7);
        sim_at(11500, []() { esp_presence_command(ESP_PRESENCE_DIAG_STREAM_OFF); });
        run_engine(15000);
        CHECK(sim_log_count("DIAG gate distance") == tables);
    }

    test_begin("damaged frames on the line are counted, not used");
    {
        RadarSim radar;
        boot(radar, true);
        run_engine(1000);
        sim_at(1000, []() {
            // A report with a wrong tail, rejected by the driver
            static const uint8_t bad[] = { 0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x01, 0x10, 0x00, 0x30,
                                           0x10, 0x00, 0x30, 0x10, 0x00, 0x00, 0x00, 0xF8, 0xF7, 0xF6, 0xF5 };
            ld2410_feed(bad, sizeof(bad));
        });
        run_engine(2000);
        CHECK(s_engine.frames_bad >= 1 && s_engine.sensor == PE_SENSOR_OK &&
              s_engine.presence == PE_PRESENCE_WAITING);
    }

    return test_end();
}
