// Host test of the local UI task (main/local_ui.cpp) with the real SH1106
// driver on a simulated I2C bus, the settings in a simulated NVS, and the
// LEDs and the display button stubbed below. Pins of the PCB; the PCB has no
// display button, so the button logic is tested with one added on GPIO20.
// run.sh also builds it with CONFIG_DISPLAY_BUTTON_GPIO=-1 (the default) and
// without the CO2 sensor (CONFIG_CO2_SENSOR_NONE).
#include "sim.h"

#define CONFIG_STATUS_LED_BRIGHTNESS 50
#define CONFIG_OLED_ROTATE_180 1
#define CONFIG_OLED_SDA_PIN 4
#define CONFIG_OLED_SCL_PIN 5
#define CONFIG_OLED_I2C_ADDRESS 0x3C
#define CONFIG_STATUS_LED_GPIO 3
#define CONFIG_STATUS_LED_COUNT 6
#define CONFIG_PM_SENSOR_UART_RX_PIN 6
#define CONFIG_PM_SENSOR_UART_TX_PIN 7
#define CONFIG_BUTTON_GPIO 10
#ifndef CONFIG_DISPLAY_BUTTON_GPIO
#define CONFIG_DISPLAY_BUTTON_GPIO 20
#endif
#ifndef CONFIG_CO2_SENSOR_NONE
#define CONFIG_CO2_SENSOR_SCD4X 1
#endif

#include "drivers/button.h"
#include "drivers/status_led.h"

// Display button: keep the callback, press() calls it as the driver does.
static button_cb_t s_button_cb;
static int s_button_gpio = -1;
esp_err_t button_init(int gpio, button_cb_t cb, void *)
{
    s_button_gpio = gpio;
    s_button_cb = cb;
    return ESP_OK;
}
[[maybe_unused]] static void button_event(button_event_type_t type, uint32_t held_ms, int clicks)
{
    const button_event_t event = { type, held_ms, clicks };
    s_button_cb(&event, NULL);
}
#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
[[maybe_unused]] static void press()
{
    button_event(BUTTON_HELD, 0, 0);    // the press itself
    button_event(BUTTON_HELD, 50, 0);
    button_event(BUTTON_HELD, 100, 0);
    button_event(BUTTON_RELEASED, 150, 0);
    button_event(BUTTON_CLICKS, 0, 1);
}
#endif

// LEDs: remember the last color and how it was set.
struct Led {
    float r = 0, g = 0, b = 0;
    int fades = 0, sets = 0, refreshes = 0;
};
static Led s_led;
esp_err_t status_led_init(int, int) { return ESP_OK; }
void status_led_set(float r, float g, float b, float) { s_led.r = r; s_led.g = g; s_led.b = b; s_led.fades++; }
void status_led_set_now(float r, float g, float b, float) { s_led.r = r; s_led.g = g; s_led.b = b; s_led.sets++; }
void status_led_refresh(void) { s_led.refreshes++; }

// The I2C bus module, with its TAG renamed: the test resets its bus.
#define TAG I2C_BUS_TAG
#include "i2c_bus.cpp"
#undef TAG

#include "local_ui.cpp"

static const char *const kSaved = "local_ui/display_on";
static const char *const kShowCo2 = "local_ui/show_co2";

[[maybe_unused]] static bool visible() { return g_sim.panel_on && g_sim.panel_configured; }

// Runs the UI task until end_ms, calling step() at every tick. before_init()
// runs after the simulator reset, e.g. to fill the NVS.
static void run(int64_t end_ms, std::function<void()> step, std::function<void()> before_init = nullptr)
{
    sim_reset(end_ms);
    s_led = Led();
    s_screen = { NAN, -1, NAN, false, LOCAL_UI_RESET_NONE, 0, false, "" };
    s_identify_until_ms = 0;
    s_display_presses = 0;
    s_layout_toggles = 0;
    s_button_cb = nullptr;
    s_button_gpio = -1;
    s_bus = NULL;
    if (before_init) {
        before_init();
    }
    g_sim.on_tick = step;
    i2c_bus_init(CONFIG_OLED_SDA_PIN, CONFIG_OLED_SCL_PIN);
    CHECK(local_ui_init() == ESP_OK);
    CHECK(CONFIG_DISPLAY_BUTTON_GPIO < 0 || (s_button_cb != nullptr && s_button_gpio == CONFIG_DISPLAY_BUTTON_GPIO));
    try {
        local_ui_task(NULL);
    } catch (SimEnd &) {
    }
    CHECK(g_sim.wdt_resets > 0);
}

[[maybe_unused]] static void save_off() { g_sim.nvs[kSaved] = 0; }

// Measurement screen with these values.
[[maybe_unused]] static screen_t values(float pm25, float co2)
{
    screen_t s = { pm25, local_ui_pm25_level(pm25), co2, false, LOCAL_UI_RESET_NONE, 0, false, "" };
    return s;
}

// True when the frame last sent to the OLED is what draw_screen() draws for
// this screen.
[[maybe_unused]] static bool shows(const screen_t &screen, bool show_co2)
{
    uint8_t sent[8][128];
    memcpy(sent, g_sim.oled_frame, sizeof(sent));
    draw_screen(screen, show_co2);      // sends the expected frame
    return memcmp(sent, g_sim.oled_frame, sizeof(sent)) == 0;
}

// Lit pixels of the frame last sent to the OLED in a rectangle (inclusive).
[[maybe_unused]] static int ink(int x0, int x1, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            n += (g_sim.oled_frame[y / 8][x] >> (y % 8)) & 1;
        }
    }
    return n;
}

// Pairing screen with this code.
[[maybe_unused]] static screen_t pairing(const char *code)
{
    screen_t s = values(NAN, NAN);
    s.pairing = true;
    snprintf(s.pairing_code, sizeof(s.pairing_code), "%s", code);
    return s;
}

#if CONFIG_CO2_SENSOR_NONE

int main()
{
    printf("local_ui (no CO2 sensor):\n");
    // PM2.5 alone, whatever is saved; switching does nothing.
    bool single_at_41 = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 40000) local_ui_toggle_layout();
        if (sim_ms() == 41500) single_at_41 = shows(values(31, NAN), false);
    }, [] { g_sim.nvs[kShowCo2] = 1; });
    CHECK(single_at_41 && g_sim.nvs_writes == 0);
    CHECK(sim_log_contains("No CO2 sensor"));

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}

#elif CONFIG_DISPLAY_BUTTON_GPIO < 0

int main()
{
    printf("local_ui (no display button):\n");
    // Nothing can turn the OLED back on: a setting saved with a button is ignored.
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
    }, save_off);
    CHECK(visible() && s_button_cb == nullptr && g_sim.nvs_writes == 0);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}

#else

int main()
{
    printf("local_ui:\n");
    // Column offset of the controller: 0 on an SSD1306, 2 on an SH1106.
    run(3000, [] {});
#if CONFIG_OLED_CONTROLLER_SSD1306
    CHECK(g_sim.oled_column == 0);
#else
    CHECK(g_sim.oled_column == 2);
#endif
    CHECK(g_sim.i2c_buses == 1);

    // Start-up: on, then stays on (setting on when never saved); LED takes the level color.
    bool on_at_30 = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 30000) on_at_30 = visible();
    });
    CHECK(on_at_30 && visible());
    CHECK(s_led.fades == 1 && s_led.r == 0.7f && s_led.g == 1.0f);     // KHÁ: yellow-green
    CHECK(s_led.refreshes >= 4);
    CHECK(g_sim.nvs_writes == 0);

    // PM2.5 + CO2 when never saved; "--" until the first CO2 value, and again
    // when the sensor stops answering (NAN).
    bool dashes_at_20 = false, both_at_41 = false;
    run(60000, [&] {
        if (sim_ms() == 20000) dashes_at_20 = shows(values(NAN, NAN), true);
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 40000) local_ui_set_co2(612);
        if (sim_ms() == 41500) both_at_41 = shows(values(31, 612), true);
        if (sim_ms() == 50000) local_ui_set_co2(NAN);
    });
    CHECK(dashes_at_20 && both_at_41 && shows(values(31, NAN), true));
    // PM2.5 on the left, "--" for CO2 on the right, between the names and the line.
    CHECK(ink(0, 63, 13, 46) > 2 * ink(64, 127, 13, 46) && ink(64, 127, 13, 46) > 0);
    CHECK(!shows(values(31, 612), true));   // the frames do differ
    CHECK(g_sim.nvs_writes == 0);

    // Switching: PM2.5 alone, then PM2.5 + CO2 again; each change is saved.
    bool single_at_41 = false, saved_single = false;
    run(60000, [&] {
        if (sim_ms() == 35000) {
            local_ui_set_pm25(31);
            local_ui_set_co2(612);
        }
        // Between two redraws (every second): the switch is drawn at once.
        if (sim_ms() == 40500) local_ui_toggle_layout();
        if (sim_ms() == 41000) {
            single_at_41 = shows(values(31, 612), false);
            saved_single = g_sim.nvs.count(kShowCo2) && g_sim.nvs[kShowCo2] == 0;
        }
        if (sim_ms() == 50000) local_ui_toggle_layout();
    });
    CHECK(single_at_41 && saved_single);
    CHECK(shows(values(31, 612), true) && g_sim.nvs[kShowCo2] == 1 && g_sim.nvs_writes == 2);
    CHECK(sim_log_contains("Screen: PM2.5 + CO2") && sim_log_contains("Screen: PM2.5"));

    // Saved PM2.5 alone: the screen of the version without CO2.
    run(60000, [&] {
        if (sim_ms() == 35000) {
            local_ui_set_pm25(31);
            local_ui_set_co2(612);
        }
    }, [] { g_sim.nvs[kShowCo2] = 0; });
    CHECK(shows(values(31, 612), false) && g_sim.nvs_writes == 0);

    // Pairing mode: its screen at once, then back to the measurements.
    bool pairing_at_40 = false, values_at_51 = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 39500) local_ui_set_pairing("34970112332");
        // The code is drawn: the frame differs from the one without it.
        if (sim_ms() == 40000) {
            pairing_at_40 = shows(pairing("34970112332"), true) && visible() && !shows(pairing(""), true);
        }
        if (sim_ms() == 50000) local_ui_set_pairing(NULL);
        if (sim_ms() == 50500) values_at_51 = shows(values(31, NAN), true);
    });
    CHECK(pairing_at_40 && values_at_51);
    CHECK(sim_log_contains("Pairing mode on") && sim_log_contains("Pairing mode off"));

    // Pairing mode turns the OLED on while the setting is off, and the
    // factory reset screen comes first.
    bool on_while_pairing = false, reset_first = false;
    run(70000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 50000) local_ui_set_pairing("");
        if (sim_ms() == 51000) on_while_pairing = visible() && shows(pairing(""), true);
        if (sim_ms() == 52000) local_ui_set_reset(LOCAL_UI_RESET_HOLD, 3);
        if (sim_ms() == 52500) {
            screen_t s = pairing("");
            s.reset = LOCAL_UI_RESET_HOLD;
            s.reset_seconds = 3;
            reset_first = shows(s, true) && !shows(pairing(""), true);
        }
        if (sim_ms() == 53000) local_ui_set_reset(LOCAL_UI_RESET_NONE, 0);
        if (sim_ms() == 60000) local_ui_set_pairing(NULL);
    }, save_off);
    CHECK(on_while_pairing && reset_first && !g_sim.panel_on);

    // Button: off, then on again; each change is saved.
    bool off_at_51 = false, saved_off = false, on_at_61 = false;
    run(70000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 50000) press();
        if (sim_ms() == 51000) {
            off_at_51 = !g_sim.panel_on;
            saved_off = g_sim.nvs.count(kSaved) && g_sim.nvs[kSaved] == 0;
        }
        if (sim_ms() == 60000) press();
        if (sim_ms() == 61000) on_at_61 = visible();
    });
    CHECK(off_at_51 && saved_off && on_at_61 && visible());
    CHECK(g_sim.nvs[kSaved] == 1 && g_sim.nvs_writes == 2);
    CHECK(sim_log_contains("Display button: off") && sim_log_contains("Display button: on"));

    // The press itself switches the screen, while the button is still held,
    // and holding it does not switch again.
    bool off_while_held = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() >= 45000 && sim_ms() < 48000) button_event(BUTTON_HELD, (uint32_t)(sim_ms() - 45000), 0);
        if (sim_ms() == 46000) off_while_held = !g_sim.panel_on;
        if (sim_ms() == 48000) button_event(BUTTON_RELEASED, 3000, 0);
    });
    CHECK(off_while_held && !g_sim.panel_on && g_sim.nvs_writes == 1);

    // Saved off: start-up screen, then off.
    on_at_30 = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 30000) on_at_30 = visible();
    }, save_off);
    CHECK(on_at_30 && !g_sim.panel_on && g_sim.nvs_writes == 0);

    // A press during start-up turns the screen off at once, and it stays off.
    bool off_at_11 = false;
    run(60000, [&] {
        if (sim_ms() == 10000) press();
        if (sim_ms() == 11000) off_at_11 = !g_sim.panel_on;
        if (sim_ms() == 35000) local_ui_set_pm25(31);
    });
    CHECK(off_at_11 && !g_sim.panel_on && g_sim.nvs[kSaved] == 0);

    // Saved off, pressed during start-up: off at once, nothing new to save.
    off_at_11 = false;
    run(60000, [&] {
        if (sim_ms() == 10000) press();
        if (sim_ms() == 11000) off_at_11 = !g_sim.panel_on;
    }, save_off);
    CHECK(off_at_11 && !g_sim.panel_on && g_sim.nvs_writes == 0);

    // Two presses before the task runs: off, then on again, and it stays on.
    run(60000, [&] {
        if (sim_ms() == 10000) {
            press();
            press();
        }
        if (sim_ms() == 35000) local_ui_set_pm25(31);
    }, save_off);
    CHECK(visible() && g_sim.nvs[kSaved] == 1);

    // Factory reset countdown turns the OLED on while the setting is off.
    bool on_during_reset = false;
    run(70000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 50000) local_ui_set_reset(LOCAL_UI_RESET_HOLD, 4);
        if (sim_ms() == 51000) on_during_reset = visible();
        if (sim_ms() == 52000) local_ui_set_reset(LOCAL_UI_RESET_NONE, 0);
    }, save_off);
    CHECK(on_during_reset && !g_sim.panel_on);

    // OLED unplugged for 10 s while on: re-initialized and on again.
    run(80000, [&] {
        g_sim.oled_present = !(sim_ms() >= 50000 && sim_ms() < 60000);
    });
    CHECK(visible());
    CHECK(sim_log_contains("re-initializing"));

    // Panel silently reset (power glitch, writes still acknowledged) while on.
    int64_t back_at = -1;
    run(80000, [&] {
        if (sim_ms() == 50000) {
            g_sim.panel_on = false;
            g_sim.panel_configured = false;
        }
        if (sim_ms() > 50000 && back_at < 0 && visible()) back_at = sim_ms();
    });
    CHECK(back_at > 50000 && back_at <= 61000);

    // Panel reset while off: settings sent again when the button turns it on.
    bool on_at_101 = false;
    run(110000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 50000) press();
        if (sim_ms() == 80000) {
            g_sim.panel_on = false;
            g_sim.panel_configured = false;
        }
        if (sim_ms() == 100000) press();
        if (sim_ms() == 101000) on_at_101 = visible();
    });
    CHECK(on_at_101);

    // No I2C bus (it could not be created): no OLED, the LEDs still work.
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
    }, [] { g_sim.i2c_bus_ok = false; });
    CHECK(!s_oled_ok && !g_sim.panel_on && s_led.fades == 1 && sim_log_contains("OLED init failed"));

    // Identify: white blink, then back to the air quality color.
    int white = 0, dark = 0;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 40000) local_ui_identify(3000);
        if (sim_ms() > 40000 && sim_ms() < 43000) {
            white += s_led.r == 1.0f && s_led.g == 1.0f && s_led.b == 1.0f;
            dark += s_led.r == 0.0f && s_led.g == 0.0f && s_led.b == 0.0f;
        }
    });
    CHECK(white >= 2 && dark >= 2);
    CHECK(s_led.r == 0.7f && s_led.g == 1.0f && s_led.fades == 2);

    // Identify before any value: the LEDs end off.
    run(20000, [&] {
        if (sim_ms() == 5000) local_ui_identify(2000);
    });
    CHECK(s_led.sets >= 3 && s_led.r == 0.0f && s_led.g == 0.0f && s_led.b == 0.0f);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}

#endif
