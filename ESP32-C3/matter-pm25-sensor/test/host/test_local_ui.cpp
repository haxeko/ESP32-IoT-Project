// Host test of the local UI task (main/local_ui.cpp) with the real SH1106
// driver on a simulated I2C bus, the display setting in a simulated NVS, and
// the LEDs and the display button stubbed below. Pins of the PCB; the PCB has
// no display button, so the button logic is tested with one added on GPIO20,
// and run.sh also builds it with CONFIG_DISPLAY_BUTTON_GPIO=-1 (the default).
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
#define CONFIG_FACTORY_RESET_BUTTON_GPIO -1
#ifndef CONFIG_DISPLAY_BUTTON_GPIO
#define CONFIG_DISPLAY_BUTTON_GPIO 20
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
#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
static void press()
{
    s_button_cb(0, false, NULL);    // the press itself
    s_button_cb(50, false, NULL);
    s_button_cb(100, false, NULL);
    s_button_cb(150, true, NULL);   // released
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

#include "local_ui.cpp"

static const char *const kSaved = "local_ui/display_on";

static bool visible() { return g_sim.panel_on && g_sim.panel_configured; }

// Runs the UI task until end_ms, calling step() at every tick. before_init()
// runs after the simulator reset, e.g. to fill the NVS.
static void run(int64_t end_ms, std::function<void()> step, std::function<void()> before_init = nullptr)
{
    sim_reset(end_ms);
    s_led = Led();
    s_screen = { NAN, -1, false, LOCAL_UI_RESET_NONE, 0 };
    s_identify_until_ms = 0;
    s_display_presses = 0;
    s_button_cb = nullptr;
    s_button_gpio = -1;
    if (before_init) {
        before_init();
    }
    g_sim.on_tick = step;
    CHECK(local_ui_init() == ESP_OK);
    CHECK(CONFIG_DISPLAY_BUTTON_GPIO < 0 || (s_button_cb != nullptr && s_button_gpio == CONFIG_DISPLAY_BUTTON_GPIO));
    try {
        local_ui_task(NULL);
    } catch (SimEnd &) {
    }
    CHECK(g_sim.wdt_resets > 0);
}

static void save_off() { g_sim.nvs[kSaved] = 0; }

#if CONFIG_DISPLAY_BUTTON_GPIO < 0

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
        if (sim_ms() >= 45000 && sim_ms() < 48000) s_button_cb((uint32_t)(sim_ms() - 45000), false, NULL);
        if (sim_ms() == 46000) off_while_held = !g_sim.panel_on;
        if (sim_ms() == 48000) s_button_cb(3000, true, NULL);
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
