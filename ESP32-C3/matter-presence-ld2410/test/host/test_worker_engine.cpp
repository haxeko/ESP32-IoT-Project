// The radar_cfg task with the ESP32 presence engine (CONFIG_PRESENCE_ESP_ENGINE) against the simulated radar, the
// engine replaced by a fake that records what the task hands it: the radar settings after each session, the busy
// flag around sessions, the engineering mode turned on when reports lack energies, the recovery of a silent radar
// (UART resynchronised, configuration mode left, restarts at growing intervals), and the learning, cancel and
// factory requests routed to the engine instead of the radar's own thresholds.
#include "sim.h"

#include "radar_worker.cpp"

#include <string.h>

#include <vector>

#include "esp_presence.h"
#include "radar_sim.h"

void status_led_set_learning(status_led_learning_t learning) {}

static void view_settings(const radar_settings_t *settings) {}
static void view_learn_mode(uint8_t mode) {}

// Fake engine
static bool g_needs_engineering;
static uint32_t g_silent_ms;
static int g_busy_on;
static bool g_busy;
static int g_configured;
static radar_settings_t g_settings;
static std::vector<int> g_commands;
static void (*g_learn_view)(uint8_t);

bool esp_presence_needs_engineering(void)
{
    return g_needs_engineering;
}
uint32_t esp_presence_silent_ms(void)
{
    return g_silent_ms;
}
void esp_presence_set_radar_busy(bool busy)
{
    g_busy_on += busy ? 1 : 0;
    g_busy = busy;
}
void esp_presence_configure(const radar_settings_t *settings)
{
    g_configured++;
    g_settings = *settings;
}
bool esp_presence_command(esp_presence_command_t command)
{
    g_commands.push_back(command);
    return true;
}
void esp_presence_set_learn_view(void (*show)(uint8_t))
{
    g_learn_view = show;
}

static void boot(RadarSim &radar)
{
    sim_reset(true);
    static bool driver_started;
    if (!driver_started) {
        CHECK(ld2410_init(3, 4) == ESP_OK);
        driver_started = true;
    }
    radar.bluetooth = false;
    radar.stored_bluetooth = false;
    radar.attach();
    radar.engineering = true;
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
    s_engineering_tried_ms = -kEngineeringRetryMs;
    s_recover_step = 0;
    s_recover_next_ms = 0;
    s_restart_delay_ms = 0;
    s_config_end_ms = 0;
    g_needs_engineering = false;
    g_silent_ms = 0;
    g_busy_on = 0;
    g_busy = false;
    g_configured = 0;
    g_settings = {};
    g_commands.clear();
    g_learn_view = nullptr;
}

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

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("radar worker with the ESP32 presence engine:\n");

    test_begin("boot: settings to the engine, busy while the session runs, learn view set");
    {
        RadarSim radar;
        boot(radar);
        radar.max_moving_gate = 6;
        radar.max_still_gate = 5;
        run_worker(5000);
        CHECK(g_configured == 1 && g_settings.gate_cm == 75);
        CHECK(g_settings.config.max_moving_gate == 6 && g_settings.config.max_still_gate == 5);
        CHECK(g_busy_on >= 1 && !g_busy);
        CHECK(g_learn_view == view_learn_mode);
        CHECK(radar.count(0x0062) == 0); // energies arrive: nothing to turn on
    }

    test_begin("reports without energies: engineering mode on, at most every 10 s");
    {
        RadarSim radar;
        boot(radar);
        radar.engineering = false;
        g_needs_engineering = true;
        run_worker(25000);
        CHECK(radar.engineering && radar.count(0x0062) == 3);
        CHECK(sim_log_contains("Engineering mode on: the energy of every gate for the ESP32 presence engine"));
        CHECK(!g_busy);
    }

    test_begin("silent radar: resync and leave the configuration mode after 5 s, then restarts at growing intervals");
    {
        RadarSim radar;
        boot(radar);
        bool early = true;
        bool resynced = false;
        int restarts_23s = -1;
        int restarts_50s = -1;
        int restarts_56s = -1;
        sim_at(4000, []() { g_silent_ms = 5000; });
        // Not before 5 s after the boot session either
        sim_at(5000, [&early]() { early = sim_log_contains("No valid report"); });
        sim_at(7000, [&]() {
            resynced = sim_log_contains("No valid report from the LD2410 for 5 s: resynchronising the UART") &&
                       radar.count(0x00A3) == 0;
            g_silent_ms = 20000; // silence goes on: a restart 15 s later, the next 30 s after that
        });
        sim_at(23000, [&]() { restarts_23s = radar.count(0x00A3); });
        sim_at(50000, [&]() { restarts_50s = radar.count(0x00A3); });
        sim_at(56000, [&]() {
            restarts_56s = radar.count(0x00A3);
            g_silent_ms = 0; // reports again
        });
        run_worker(60000);
        CHECK(!early && resynced);
        CHECK(restarts_23s == 1 && restarts_50s == 1 && restarts_56s == 2 && radar.restarts == 2);
        CHECK(sim_log_contains("restarting the radar (check its power and the wire from radar TX to GPIO3)"));
        CHECK(sim_log_contains("The LD2410 reports again") && s_recover_step == 0);
        CHECK(!g_busy);
    }

    test_begin("long configuration session: the silence counts from its end, reports back meanwhile: no recovery");
    {
        RadarSim radar;
        boot(radar);
        // A Range change at 6 s of silence (a restart or commands timing out make a session long); reports come
        // back 1.5 s after it
        esp_err_t range = ESP_FAIL;
        sim_at(3000, [&range]() {
            g_silent_ms = 6000;
            range = radar_worker_select(RADAR_SELECT_RANGE, 5);
        });
        sim_at(4500, []() { g_silent_ms = 0; });
        run_worker(15000);
        CHECK(range == ESP_OK && g_configured == 2 && g_settings.config.max_moving_gate == 5);
        CHECK(!sim_log_contains("No valid report") && radar.count(0x00A3) == 0 && s_recover_step == 0);
    }

    test_begin("radar stuck in configuration mode (no reports): leaving it brings the reports back");
    {
        RadarSim radar;
        boot(radar);
        // Left there after the boot session (e.g. its last command lost): no reports, and no session to end it
        sim_at(3000, [&radar]() {
            radar.config_mode = true;
            g_silent_ms = 6000;
        });
        run_worker(9000);
        CHECK(!radar.config_mode && radar.count(0x00FE) >= 2); // the boot session's, then the recovery's
        CHECK(sim_log_contains("resynchronising the UART and leaving the configuration mode"));
    }

    test_begin("Learning, Off and Factory thresholds from Home Assistant, 3 and 4 clicks: routed to the engine");
    {
        RadarSim radar;
        boot(radar);
        run_worker(3000);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_ON) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_OFF) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, RADAR_LEARN_FACTORY) == ESP_OK);
        CHECK(radar_worker_select(RADAR_SELECT_LEARN, 7) == ESP_ERR_INVALID_ARG);
        CHECK(radar_worker_request_learning());
        CHECK(radar_worker_request_factory_thresholds());
        run_worker(130000);
        const std::vector<int> expected = { ESP_PRESENCE_LEARN, ESP_PRESENCE_LEARN_CANCEL, ESP_PRESENCE_PROFILE_RESET,
                                            ESP_PRESENCE_LEARN, ESP_PRESENCE_PROFILE_RESET };
        CHECK(g_commands == expected);
        // The radar's own learning and thresholds are left alone
        CHECK(radar.count(0x000B) == 0 && radar.count(0x0064) == 0);
        CHECK(memcmp(radar.moving, RadarSim().moving, sizeof(radar.moving)) == 0);
    }

    return test_end();
}
