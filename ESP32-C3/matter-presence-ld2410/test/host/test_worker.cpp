// The radar_cfg task (main/radar_worker.cpp) with the real settings session, learning and driver against the
// simulated radar: Home Assistant choices and button requests are scheduled events, the "last choice wins", the
// Bluetooth choice in NVS, retries while the radar does not answer.
#include "sim.h"

#include "radar_worker.cpp"

#include <string.h>

#include "radar_sim.h"

// Status LED and Mode Select view: recorded
static std::vector<status_led_learning_t> s_led;
void status_led_set_learning(status_led_learning_t learning)
{
    s_led.push_back(learning);
}

static std::vector<uint8_t> s_learn_modes;
static std::vector<radar_settings_t> s_shown;
static void view_settings(const radar_settings_t *settings)
{
    s_shown.push_back(*settings);
}
static void view_learn_mode(uint8_t mode)
{
    s_learn_modes.push_back(mode);
}

static void reset_worker()
{
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
    s_led.clear();
    s_learn_modes.clear();
    s_shown.clear();
}

// New boot of the ESP32 (NVS cleared unless keep_nvs); the radar keeps its state. Bluetooth of a new radar off, so
// the boot does not restart it, unless the test sets it.
static void boot(RadarSim &radar, bool keep_nvs = false)
{
    sim_reset(!keep_nvs);
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.attach();
    reset_worker();
}

static void radar_bluetooth_off(RadarSim &radar)
{
    radar.bluetooth = false;
    radar.stored_bluetooth = false;
}

// Starts the worker and runs its task until end_ms.
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

static std::string modes(const std::vector<uint8_t> &v)
{
    std::string s;
    for (uint8_t m : v) {
        s += std::to_string(m);
    }
    return s;
}

static std::string leds()
{
    static const char *names[] = { "none", "waiting", "recording", "done", "failed", "factory" };
    std::string s;
    for (status_led_learning_t l : s_led) {
        s += s.empty() ? "" : " ";
        s += names[l];
    }
    return s;
}

static void set_learned_thresholds(RadarSim &radar)
{
    radar.moving[3] = 60;
    radar.still[3] = 70;
}

static bool factory_thresholds(const RadarSim &radar)
{
    static const uint8_t moving[9] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
    static const uint8_t still[9] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };
    return memcmp(radar.moving, moving, 9) == 0 && memcmp(radar.still + 2, still + 2, 7) == 0;
}

static void test_boot_and_settings()
{
    test_begin("boot: settings read and shown, radar Bluetooth turned off (default)");
    {
        RadarSim radar;
        boot(radar);
        run_worker(10000);
        CHECK(radar.restarts == 1 && !radar.bluetooth);
        CHECK(s_shown.size() == 2 && s_shown[0].bluetooth_on && !s_shown[1].bluetooth_on);
        CHECK(sim_log_contains("LD2410 settings: no-one duration 5 s, range moving 6 m (gate 8), still 6 m (gate 8), "
                               "75 cm per gate, Bluetooth on, MAC 8F:27:2E:B8:0F:65"));
        CHECK(sim_log_contains("Turning the LD2410 Bluetooth off (default, or as chosen in Home Assistant)"));
        CHECK(sim_log_contains("New LD2410 settings: no-one duration 5 s, range moving 6 m (gate 8), still 6 m "
                               "(gate 8), 75 cm per gate, Bluetooth off"));
        CHECK(g_sim.nvs.count("ld2410") == 0); // nothing chosen: nothing stored
    }

    test_begin("boot: radar Bluetooth left on when chosen in Home Assistant (NVS)");
    {
        RadarSim radar;
        boot(radar);
        g_sim.nvs["ld2410"]["bt_on"] = { 1 };
        run_worker(10000);
        CHECK(radar.restarts == 0 && radar.bluetooth && s_shown.size() == 1);
    }

    test_begin("boot: radar not answering, read again after 15 s, 30 s, 1 min ... up to 5 min; changes rejected");
    {
        RadarSim radar;
        boot(radar);
        radar.tx_wire_broken = true;
        esp_err_t range = ESP_OK;
        sim_at(5000, [&range]() { range = radar_worker_select(RADAR_SELECT_RANGE, 5); });
        sim_at(600000, [&radar]() { radar.tx_wire_broken = false; });
        run_worker(1500000);
        CHECK(range == ESP_ERR_INVALID_STATE);
        for (const char *wait : { "next try in 15 s", "next try in 30 s", "next try in 60 s", "next try in 120 s",
                                  "next try in 240 s" }) {
            CHECK(sim_log_count(wait) == 1);
        }
        CHECK(sim_log_count("next try in 300 s") == 1); // the wire is fixed before the read after it
        CHECK(sim_log_contains("Could not read the LD2410 settings (check the wire from GPIO4 to the radar RX pin)"));
        CHECK(sim_log_contains("LD2410 settings: no-one duration 5 s") && s_synced);
        CHECK(radar_worker_select(RADAR_SELECT_RANGE, 5) == ESP_OK);
    }

    test_begin("Range: written, read back and shown; invalid modes refused, custom mode ignored");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        esp_err_t results[4] = {};
        sim_at(5000, [&results]() {
            results[0] = radar_worker_select(RADAR_SELECT_RANGE, 1);
            results[1] = radar_worker_select(RADAR_SELECT_RANGE, 9);
            results[2] = radar_worker_select(RADAR_SELECT_RANGE, RADAR_MODE_CUSTOM);
        });
        sim_at(6000, [&results]() { results[3] = radar_worker_select(RADAR_SELECT_RANGE, 5); });
        run_worker(10000);
        CHECK(results[0] == ESP_ERR_INVALID_ARG && results[1] == ESP_ERR_INVALID_ARG && results[2] == ESP_OK);
        CHECK(results[3] == ESP_OK && radar.max_moving_gate == 5 && radar.max_still_gate == 5);
        CHECK(s_shown.size() == 2 && s_shown[1].config.max_moving_gate == 5);
        CHECK(sim_log_contains("New LD2410 settings: no-one duration 5 s, range moving 3.75 m (gate 5), still 3.75 m "
                               "(gate 5), 75 cm per gate, Bluetooth off"));
        CHECK(radar.count(0x0060) == 1);
    }

    test_begin("Radar resolution 0.2 m: radar restarted, shown, Range labels follow");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        esp_err_t bad = ESP_OK;
        sim_at(5000, [&bad]() {
            bad = radar_worker_select(RADAR_SELECT_RESOLUTION, 2);
            radar_worker_select(RADAR_SELECT_RESOLUTION, RADAR_RESOLUTION_FINE);
        });
        run_worker(10000);
        CHECK(bad == ESP_ERR_INVALID_ARG);
        CHECK(radar.resolution == 1 && radar.restarts == 1 && s_shown.back().gate_cm == 20);
        CHECK(sim_log_contains("range moving 1.6 m (gate 8), still 1.6 m (gate 8), 20 cm per gate, Bluetooth off"));
    }

    test_begin("Radar resolution on a fixed 0.75 m radar: not supported, real value shown again, not retried");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        radar.has_resolution = false;
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_RESOLUTION, RADAR_RESOLUTION_FINE); });
        run_worker(60000);
        CHECK(sim_log_contains("Changing the radar settings failed, err:262"));
        CHECK(s_shown.size() == 2 && s_shown[1].gate_cm == 75 && !s_shown[1].resolution_settable);
        CHECK(!sim_log_contains("next try") && !sim_log_contains("read again"));
        CHECK(sim_log_contains("75 cm per gate (fixed)"));
    }

    test_begin("Radar Bluetooth on from Home Assistant: kept in NVS and at the next boot");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_BLUETOOTH, RADAR_BLUETOOTH_ON); });
        run_worker(10000);
        CHECK(radar.bluetooth && radar.restarts == 1 && s_shown.back().bluetooth_on);
        CHECK(g_sim.nvs["ld2410"]["bt_on"] == std::vector<uint8_t>{ 1 });
        CHECK(sim_log_contains("Bluetooth on, MAC 8F:27:2E:B8:0F:65"));
        boot(radar, true); // reboot of the ESP32
        run_worker(10000);
        CHECK(radar.bluetooth && radar.restarts == 1);
    }

    test_begin("Radar Bluetooth that does not follow: not tried again before the next boot or choice");
    {
        RadarSim radar;
        boot(radar);
        radar.bluetooth_stuck = true; // on, stays on
        run_worker(300000);
        CHECK(radar.restarts == 1 && radar.bluetooth);
        CHECK(sim_log_contains("The radar Bluetooth is still on after its restart"));
        CHECK(s_shown.back().bluetooth_on);
    }

    test_begin("Radar without Bluetooth: Not available, a change is not supported");
    {
        RadarSim radar;
        boot(radar);
        radar.has_bluetooth = false;
        sim_at(10000, []() { radar_worker_select(RADAR_SELECT_BLUETOOTH, RADAR_BLUETOOTH_ON); });
        run_worker(20000);
        CHECK(!s_shown[0].bluetooth_known && radar.restarts == 0);
        CHECK(sim_log_contains("Bluetooth not available"));
        CHECK(sim_log_contains("The Bluetooth query failed (radar without Bluetooth?)"));
        CHECK(radar_worker_select(RADAR_SELECT_BLUETOOTH, RADAR_MODE_CUSTOM) == ESP_OK);
    }
}

static void test_learning_requests()
{
    test_begin("Learning from Home Assistant: radar measures, LED and select follow");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        esp_err_t result = ESP_FAIL;
        sim_at(5000, [&result]() { result = radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        run_worker(120000);
        CHECK(result == ESP_OK && modes(s_learn_modes) == "10" && leds() == "waiting recording done");
        CHECK(radar.moving[0] == 45 && sim_log_contains("Learning done: thresholds set by the LD2410"));
    }

    test_begin("Learning, then Off: cancelled, radar restarted, LED back to normal at once");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(30000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_OFF); });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "10" && leds() == "waiting recording none");
        CHECK(factory_thresholds(radar) && radar.restarts == 1);
    }

    test_begin("Learning, then Factory thresholds while the radar measures: cancelled, then written");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        set_learned_thresholds(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(40000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "120" && leds() == "waiting recording none factory");
        CHECK(factory_thresholds(radar) && radar.restarts == 1);
    }

    test_begin("Learning on the ESP32, then Factory thresholds while recording");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        radar.has_auto = false;
        set_learned_thresholds(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(40000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "120" && leds() == "waiting recording none factory");
        CHECK(factory_thresholds(radar) && !radar.engineering && radar.restarts == 0);
    }

    test_begin("Factory thresholds, then Learning before it starts: only the learning");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        sim_at(5000, []() {
            radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY);
            radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON);
        });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "10" && leds() == "waiting recording done");
        CHECK(!sim_log_contains("Restoring the LD2410 factory thresholds"));
    }

    test_begin("Factory thresholds, then Off before it starts: nothing");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        set_learned_thresholds(radar);
        sim_at(5000, []() {
            radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY);
            radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_OFF);
        });
        run_worker(30000);
        CHECK(s_learn_modes.empty() && s_led.empty() && !factory_thresholds(radar));
    }

    test_begin("Factory thresholds while a Learning is starting, then Learning again: the learning runs");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "10" && leds() == "waiting recording done");
    }

    test_begin("Learning chosen while the factory thresholds are written: no Off in between, then the learning");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        set_learned_thresholds(radar);
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        sim_at(5020, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); }); // during the writes
        run_worker(120000);
        CHECK(factory_thresholds(radar) || radar.moving[0] == 45); // written, then learned by the radar
        CHECK(modes(s_learn_modes) == "210" && leds() == "factory waiting recording done");
    }

    test_begin("Button: three clicks while learning are ignored");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        bool again = true;
        sim_at(5000, []() { CHECK(radar_worker_request_learning()); });
        sim_at(30000, [&again]() { again = radar_worker_request_learning(); });
        run_worker(120000);
        CHECK(!again && sim_log_contains("Empty room learning already requested or running"));
        CHECK(modes(s_learn_modes) == "10" && leds() == "waiting recording done");
    }

    test_begin("Button: four clicks while learning cancel it and write the factory thresholds");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        set_learned_thresholds(radar);
        sim_at(5000, []() { CHECK(radar_worker_request_learning()); });
        sim_at(30000, []() { CHECK(radar_worker_request_factory_thresholds()); });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "120" && leds() == "waiting recording none factory");
        CHECK(factory_thresholds(radar));
    }

    test_begin("Button: four clicks, then three before it starts: the learning");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        sim_at(5000, []() {
            radar_worker_request_factory_thresholds();
            radar_worker_request_learning();
        });
        run_worker(120000);
        CHECK(modes(s_learn_modes) == "10" && !sim_log_contains("Restoring the LD2410 factory thresholds"));
    }

    test_begin("Learning and factory thresholds failing: LED shows the failure");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        radar.has_auto = false;
        set_learned_thresholds(radar);
        radar.stuck_gate = 3;
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(100000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY); });
        radar.energies = [](int64_t, uint8_t *m, uint8_t *s) {
            for (int g = 0; g < 9; g++) {
                m[g] = 5;
                s[g] = 5;
            }
            s[3] = 60;
        };
        run_worker(130000);
        CHECK(modes(s_learn_modes) == "1020" && leds() == "waiting recording failed failed");
    }

    test_begin("Range chosen while learning: applied once the learning is over");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        int during = -1;
        sim_at(5000, []() { radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON); });
        sim_at(30000, []() { CHECK(radar_worker_select(RADAR_SELECT_RANGE, 4) == ESP_OK); });
        sim_at(60000, [&during, &radar]() { during = radar.max_moving_gate; });
        run_worker(120000);
        CHECK(during == 8 && radar.max_moving_gate == 4 && radar.max_still_gate == 4);
        CHECK(modes(s_learn_modes) == "10");
    }

    test_begin("Worker not running: button shows a failure, learning choices rejected, Off accepted");
    {
        RadarSim radar;
        boot(radar);
        CHECK(!radar_worker_request_learning());
        CHECK(!radar_worker_request_factory_thresholds());
        CHECK(leds() == "failed failed");
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON) == ESP_ERR_INVALID_STATE);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY) == ESP_ERR_INVALID_STATE);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_OFF) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_RANGE, 5) == ESP_ERR_INVALID_STATE);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, 3) == ESP_ERR_INVALID_ARG);
    }

    test_begin("Modes out of the lists are refused, the custom mode changes nothing");
    {
        RadarSim radar;
        boot(radar);
        radar_bluetooth_off(radar);
        run_worker(5000);
        const size_t commands = radar.commands.size();
        CHECK(radar_worker_select(RADAR_SELECT_BLUETOOTH, 2) == ESP_ERR_INVALID_ARG);
        CHECK(radar_worker_select(RADAR_SELECT_RESOLUTION, 100) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_BLUETOOTH, 100) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, 100) == ESP_ERR_INVALID_ARG);
        g_sim.end_ms = 10000;
        try {
            radar_task(nullptr);
        } catch (SimEnd &) {
        }
        // One more pass of the loop (a new boot read): nothing else was sent
        CHECK(radar.commands.size() - commands == 5);
    }
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("radar_worker:\n");
    test_boot_and_settings();
    test_learning_requests();
    return test_end();
}
