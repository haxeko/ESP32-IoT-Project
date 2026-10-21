// The presence engine on the device (esp_presence.cpp) against the simulated radar, through the LD2410 driver: the
// radar's thresholds handed over by the radar_cfg task, the noise recorded while learning saved in NVS and loaded at
// the next boot, the radar thresholds learned from the empty room, a damaged blob left alone, the occupancy handed to the
// presence task, a silent radar (no "empty"), reports without energies (engineering mode wanted), the radar busy,
// the commands and the logs.
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
// Still energy of gate 2 in the room of the report (HLKRadarTool: 56 on average, at most 63): the sum of three
// draws, bell-shaped (median, each draw +- spread); 0: as above
static int g_still2_median;
static int g_still2_spread;
// Moving gate 6: one report of 40 in every g_spike_every, alone (0: none)
static int g_spike_every;
static uint32_t g_reports;

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
    if (g_still2_median > 0) {
        const int a = noise(50, g_still2_spread) - 50;
        const int b = noise(50, g_still2_spread) - 50;
        s[2] = (uint8_t)noise(g_still2_median + a + b, g_still2_spread);
    }
    g_reports++;
    if (g_spike_every > 0 && g_reports % g_spike_every == 0) {
        m[6] = 40;
    }
    if (g_person_gate >= 0) {
        m[g_person_gate] = std::max(m[g_person_gate], g_person_moving);
        s[g_person_gate] = std::max(s[g_person_gate], g_person_still);
    }
}

// What the learning hands over: `reports` reports of the empty room, counted as the LD2410 driver does (histograms
// and the highest median of three successive reports)
static ld2410_capture_t g_capture;
static uint8_t median3(uint8_t a, uint8_t b, uint8_t c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
static const ld2410_capture_t *room_capture(uint32_t reports)
{
    memset(&g_capture, 0, sizeof(g_capture));
    uint8_t last[3][2][9] = {};
    for (uint32_t i = 0; i < reports; i++) {
        uint8_t m[9];
        uint8_t s[9];
        room_energies(0, m, s);
        for (int g = 0; g < 9; g++) {
            g_capture.moving[g][m[g]]++;
            g_capture.still[g][s[g]]++;
            for (int k = 0; k < 2; k++) {
                last[2][k][g] = last[1][k][g];
                last[1][k][g] = last[0][k][g];
                last[0][k][g] = k == 0 ? m[g] : s[g];
            }
            if (i >= 2) {
                g_capture.moving_filtered_max[g] =
                    std::max(g_capture.moving_filtered_max[g], median3(last[0][0][g], last[1][0][g], last[2][0][g]));
                g_capture.still_filtered_max[g] =
                    std::max(g_capture.still_filtered_max[g], median3(last[0][1][g], last[1][1][g], last[2][1][g]));
            }
        }
    }
    g_capture.frames = reports;
    return &g_capture;
}

static pe_radar_thresholds_t factory()
{
    pe_radar_thresholds_t t;
    pe_factory_thresholds(&t);
    return t;
}

// The learning of the radar_cfg task: hands over `reports` reports of the empty room (120 s) with the thresholds the
// radar holds (held); returns the thresholds to write to the radar
static ld2410_thresholds_t learned(uint32_t reports = 1200, const pe_radar_thresholds_t &held = factory())
{
    ld2410_thresholds_t t;
    memcpy(t.moving, held.moving, sizeof(t.moving));
    memcpy(t.still, held.still, sizeof(t.still));
    esp_presence_learned_room(room_capture(reports), 75, &t);
    return t;
}

// Every learned threshold between 75 % of the factory one and factory + 30, still gates 0-1 as they were
static bool within_learning_limits(const ld2410_thresholds_t &t, const pe_radar_thresholds_t &held = factory())
{
    const pe_radar_thresholds_t f = factory();
    bool ok = t.still[0] == held.still[0] && t.still[1] == held.still[1];
    for (int g = 0; g < 9; g++) {
        ok = ok && t.moving[g] >= (f.moving[g] * 75 + 99) / 100 && t.moving[g] <= f.moving[g] + 30;
        if (g >= 2) {
            ok = ok && t.still[g] >= (f.still[g] * 75 + 99) / 100 && t.still[g] <= f.still[g] + 30;
        }
    }
    return ok;
}

static pe_radar_thresholds_t thresholds_of(const ld2410_thresholds_t &t)
{
    pe_radar_thresholds_t r;
    memcpy(r.moving, t.moving, sizeof(r.moving));
    memcpy(r.still, t.still, sizeof(r.still));
    return r;
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
    g_still2_median = 0;
    g_spike_every = 0;
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
        CHECK(sim_log_contains("Thresholds from the radar (75 cm gates): strong from radar + 1 and at least the empty "
                               "room noise + 5; hold at radar - 20 % (at most 5), at least the noise + 2"));
        CHECK(sim_log_contains("  gate 8 6-6.75 m    moving: radar 15 strong 16 hold 15 | still: radar 20 strong 21 "
                               "hold 20"));
        CHECK(sim_log_contains("  gate 0 0-0.75 m    moving: radar 50 strong 51 hold 50 | still: no radar threshold"));
        CHECK(sim_log_contains("ESP32 presence engine: strong signal (from radar threshold + 1, at least the empty "
                               "room noise + 5) in 6 of 8 reports to start, hold signal in 3 of 10 reports to stay"));
        CHECK(sim_log_contains("Fast start: moving energy at the strong level + 20 in 3 reports in a row"));
        CHECK(sim_log_contains("Person's gate +- 1: hold signal in 1 of the last 10 reports there"));
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
        // The radar_cfg task: the learning (thresholds learned, written), then moving gate 4 set to 25 with
        // HLKRadarTool and read again
        sim_at(2000, []() {
            pe_radar_thresholds_t t = factory();
            t.moving[4] = 25;
            const ld2410_thresholds_t written = learned(300, t);
            CHECK(within_learning_limits(written, t));
            const radar_settings_t settings = settings_with(t);
            esp_presence_configure(&settings);
        });
        run_engine(5000);
        CHECK(sim_log_contains("Empty room, 300 reports: threshold = max + the larger of 8 and (max - avg), at least "
                               "the noise + 4, from 75 % of factory to factory + 30"));
        CHECK(sim_log_contains("  gate 0 0-0.75 m    moving: avg 28 max ") && sim_log_contains("| still: not settable"));
        pe_noise_t n;
        CHECK(stored_noise(&n) && pe_noise_valid(&n) && n.frames == 300 && n.gate_cm == 75);
        CHECK(s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 16);
        CHECK(pe_trigger_threshold(&s_engine, PE_MOVING, 4) == 26 && pe_hold_threshold(&s_engine, PE_MOVING, 4) == 20);
        CHECK(sim_log_contains("Noise of the empty room recorded (300 reports) and saved"));
        CHECK(sim_log_contains("  gate 4 3-3.75 m    moving: radar 25 strong 26 hold 20 (noise "));
        CHECK(sim_log_count("Thresholds from the radar") == 2); // boot, then once for both changes
        CHECK(sim_log_count("The radar holds its factory thresholds") == 1); // only at boot

        RadarSim again;
        boot(again, false);
        CHECK(sim_log_contains("Noise of the empty room loaded (300 reports, 75 cm gates)"));
        run_engine(3000);
        CHECK(s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 16);
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
            learned(300);
            const radar_settings_t settings = settings_with(factory(), 20);
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        CHECK(s_engine.has_noise && !s_engine.th.noise && pe_hold_threshold(&s_engine, PE_STILL, 7) == 20);
        CHECK(sim_log_contains("The noise was recorded with 75 cm gates and the radar uses 20 cm gates"));
    }

    test_begin("noise above a radar threshold lowered afterwards (HLKRadarTool): strong only from the noise + 5, "
               "warned");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() {
            learned(300);
            pe_radar_thresholds_t t = factory();
            t.moving[0] = 25; // the noise of gate 0 is about 28
            const radar_settings_t settings = settings_with(t);
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        const uint8_t level = s_engine.noise.level[PE_MOVING][0];
        CHECK(level > 28 && level < 50);
        CHECK(pe_trigger_threshold(&s_engine, PE_MOVING, 0) == level + 5);
        CHECK(pe_hold_threshold(&s_engine, PE_MOVING, 0) == level + 2);
        char text[160];
        snprintf(text, sizeof(text), "Gate 0 moving: the empty room noise %u reaches the radar threshold 25, strong "
                                     "there only from %u", level, level + 5);
        CHECK(sim_log_contains(text));
        CHECK(sim_log_contains("  gate 0 0-0.75 m    moving: radar 25 strong "));
        CHECK(sim_log_contains(", strong and hold raised) | still: no radar threshold"));
    }

    test_begin("room of the example (still gate 2: 56 on average, at most 62; radar 40): presence starts without "
               "anyone until learned; the learning sets that radar threshold above the noise, then never present");
    {
        RadarSim radar;
        boot(radar, true); // no noise recorded, factory thresholds
        g_still2_median = 56;
        g_still2_spread = 2; // 50-62
        run_engine(5000);
        CHECK(esp_presence_occupied() && sim_log_contains("Present: still gate 2 energy")); // the false trigger
        CHECK(sim_log_contains(", strong from 41 (radar threshold 40), 6 of the last 8 reports strong"));
        // Learned: every threshold set from the room and written, then read again by the radar_cfg task
        static ld2410_thresholds_t written;
        sim_at(5000, []() {
            written = learned();
            const radar_settings_t settings = settings_with(thresholds_of(written));
            esp_presence_configure(&settings);
        });
        run_engine(8000);
        const uint8_t level = s_engine.noise.level[PE_STILL][2];
        CHECK(level >= 63 && level <= 66 && written.still[2] >= level + 4 && written.still[2] <= 70);
        CHECK(within_learning_limits(written));
        CHECK(sim_log_contains("  gate 2 1.5-2.25 m  moving: avg ") && sim_log_contains("| still: avg 56 max 6"));
        CHECK(pe_trigger_threshold(&s_engine, PE_STILL, 2) == written.still[2] + 1);
        CHECK(!(s_engine.th.ch[PE_STILL][2].flags & PE_CH_NOISE_TRIGGER));
        // Present until the hold time is over (the noise does not hold it), then never present
        esp_presence_set_hold_time(60);
        run_engine(80000);
        CHECK(!esp_presence_occupied() && s_engine.presence == PE_PRESENCE_WAITING);
        const int triggers = sim_log_count("Present: ");
        run_engine(30 * 60 * 1000);
        CHECK(sim_log_count("Present: ") == triggers && !esp_presence_occupied());
        // Someone sitting there is still seen
        sim_at(30 * 60 * 1000, []() {
            g_person_gate = 2;
            g_person_moving = 15;
            g_person_still = 85;
        });
        run_engine(30 * 60 * 1000 + 2000);
        CHECK(esp_presence_occupied());
    }

    test_begin("highest energy above factory + 30 (something moved while learning): radar threshold limited, warned; "
               "the engine starts presence there only from the noise + 5");
    {
        RadarSim radar;
        boot(radar, true);
        g_still2_median = 88;
        g_still2_spread = 1;
        static ld2410_thresholds_t written;
        sim_at(1000, []() {
            written = learned();
            const radar_settings_t settings = settings_with(thresholds_of(written));
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        const uint8_t level = s_engine.noise.level[PE_STILL][2];
        CHECK(written.still[2] == 70 && pe_trigger_threshold(&s_engine, PE_STILL, 2) == level + 5);
        CHECK(sim_log_contains("Gate 2 still: max ") &&
              sim_log_contains(": threshold limited to 70 (factory 40 + 30). Something kept moving there"));
        CHECK(sim_log_contains("-> 70 limit (was 40)"));
        char text[160];
        snprintf(text, sizeof(text), "Gate 2 still: the empty room noise %u reaches the radar threshold 70 (a learning "
                                     "raises it at most to factory + 30), strong there only from %u", level, level + 5);
        CHECK(sim_log_contains(text));
        CHECK(!esp_presence_occupied());
    }

    test_begin("noise could not be saved in NVS: the old noise stays, the thresholds are learned all the same (they "
               "are stored in the radar)");
    {
        RadarSim radar;
        boot(radar, true);
        g_still2_median = 56;
        g_still2_spread = 2;
        g_sim.nvs_full = true;
        static ld2410_thresholds_t written;
        sim_at(1000, []() { written = learned(); });
        run_engine(3000);
        CHECK(within_learning_limits(written) && written.still[2] >= 65 && !s_engine.has_noise);
        CHECK(sim_log_contains("Could not save the noise of the empty room in NVS"));
    }

    test_begin("learning: every threshold as computed from the recording (max + the larger of 8 and max - avg, at "
               "least the noise + 4, 75 % of factory to factory + 30); lone spikes left out of the max, not the noise");
    {
        RadarSim radar;
        boot(radar, true);
        g_spike_every = 16; // moving gate 6: 40 in one report of 16, alone
        static ld2410_thresholds_t written;
        sim_at(1000, []() {
            written = learned();
            const radar_settings_t settings = settings_with(thresholds_of(written));
            esp_presence_configure(&settings);
        });
        run_engine(4000);
        CHECK(s_engine.th.noise);
        const pe_radar_thresholds_t f = factory();
        int raised_to_noise = 0;
        for (int kind = 0; kind < PE_KINDS; kind++) {
            for (int g = kind == PE_STILL ? 2 : 0; g < 9; g++) {
                const uint16_t *hist = kind == PE_MOVING ? g_capture.moving[g] : g_capture.still[g];
                uint32_t n = 0;
                uint32_t sum = 0;
                int peak = 0;
                for (int v = 0; v <= 100; v++) {
                    n += hist[v];
                    sum += hist[v] * v;
                    peak = hist[v] > 0 ? v : peak;
                }
                const int avg = (int)((sum + n / 2) / n);
                const int max = std::min<int>(
                    kind == PE_MOVING ? g_capture.moving_filtered_max[g] : g_capture.still_filtered_max[g], peak);
                const int level = s_engine.noise.level[kind][g];
                const int factory_threshold = kind == PE_MOVING ? f.moving[g] : f.still[g];
                int want = max + std::max(8, max - avg);
                if (level + 4 > want) {
                    want = level + 4;
                    raised_to_noise++;
                }
                want = std::clamp(want, (factory_threshold * 75 + 99) / 100, std::min(factory_threshold + 30, 99));
                CHECK((kind == PE_MOVING ? written.moving[g] : written.still[g]) == want);
            }
        }
        CHECK(g_capture.moving_filtered_max[6] < 10 && s_engine.noise.level[PE_MOVING][6] >= 40);
        CHECK(written.moving[6] == s_engine.noise.level[PE_MOVING][6] + 4 && raised_to_noise >= 1);
        CHECK(written.moving[0] != f.moving[0] && written.still[4] != f.still[4]);
        CHECK(sim_log_contains("-> 44 noise (was 15)"));
        run_engine(10 * 60 * 1000); // the spikes go on: never present
        CHECK(!esp_presence_occupied() && sim_log_count("Present: ") == 0);
    }

    test_begin("presence: walking in occupies, sitting still (above hold, not above the radar) keeps it, empty a "
               "hold time after leaving");
    {
        RadarSim radar;
        boot(radar, true);
        sim_at(1000, []() { learned(300); });
        esp_presence_set_hold_time(60);
        run_engine(5000);
        CHECK(!esp_presence_occupied() && sim_log_contains("Hold time 60 s"));
        sim_at(5000, []() {
            g_person_gate = 4;
            g_person_moving = 70;
            g_person_still = 40;
        });
        run_engine(6000);
        CHECK(esp_presence_occupied() && sim_log_contains("Present (fast): moving gate 4 energy"));
        CHECK(sim_log_contains(", fast from 41 (radar threshold 20), 3 reports in a row"));
        sim_at(9000, []() {
            g_person_moving = 0;
            g_person_still = (uint8_t)(pe_hold_threshold(&s_engine, PE_STILL, 4) + 4);
        });
        sim_at(299000, []() { esp_presence_command(ESP_PRESENCE_PRINT_STATUS); });
        run_engine(300000);
        CHECK(esp_presence_occupied() && pe_hold_threshold(&s_engine, PE_STILL, 4) + 4 <= 30);
        CHECK(sim_log_contains("STATUS presence: present, hold signal at gate 4 (the person's gates)"));
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
        sim_at(1000, []() { learned(300); });
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
        CHECK(sim_log_contains("DIAG gate distance   | moving now filt peak hold strong radar | still now filt peak "
                               "hold strong radar"));
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
