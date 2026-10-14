// Host test of the local UI task (main/local_ui.cpp) with the real SH1106
// driver on a simulated I2C bus, and the radar and LEDs stubbed below.
#include "sim.h"

#define CONFIG_STATUS_LED_BRIGHTNESS 50
#define CONFIG_OLED_ROTATE_180 1
#define CONFIG_PRESENCE_DISTANCE_CM 120
#define CONFIG_PRESENCE_HOLD_SEC 60
#define CONFIG_RADAR_TIMEOUT_SEC 30
#define CONFIG_OLED_SDA_PIN 8
#define CONFIG_OLED_SCL_PIN 9
#define CONFIG_OLED_I2C_ADDRESS 0x3C
#define CONFIG_STATUS_LED_GPIO 10
#define CONFIG_STATUS_LED_COUNT 6
#define CONFIG_LD2410_UART_RX_PIN 3
#define CONFIG_LD2410_UART_TX_PIN 4

#include "drivers/ld2410.h"
#include "drivers/status_led.h"

// Radar: a person at 80 cm while s_person is true.
static bool s_person;
bool ld2410_get_target(ld2410_target_t *t)
{
    *t = {};
    t->moving = s_person;
    t->moving_cm = 80;
    t->timestamp_ms = sim_ms();
    return true;
}
esp_err_t ld2410_init(int, int) { return ESP_OK; }

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

static bool visible() { return g_sim.panel_on && g_sim.panel_configured; }

// Runs the UI task until end_ms, calling step() at every tick.
static void run(int64_t end_ms, std::function<void()> step)
{
    sim_reset(end_ms);
    s_person = false;
    s_led = Led();
    s_screen = { NAN, -1, false, LOCAL_UI_RESET_NONE, 0 };
    s_identify_until_ms = 0;
    g_sim.on_tick = step;
    CHECK(local_ui_init() == ESP_OK);
    try {
        local_ui_task(NULL);
    } catch (SimEnd &) {
    }
    CHECK(g_sim.wdt_resets > 0);
}

int main()
{
    printf("local_ui:\n");
    // Start-up: on until the first value + 5 s, then off; LED takes the level color.
    bool on_at_30 = false;
    run(60000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 30000) on_at_30 = visible();
    });
    CHECK(on_at_30 && !g_sim.panel_on);
    CHECK(s_led.fades == 1 && s_led.r == 0.7f && s_led.g == 1.0f);     // KHÁ: yellow-green
    CHECK(s_led.refreshes >= 4);

    // Presence: on while someone is near, off 60 s after they leave.
    bool on_at_105 = false, on_at_165 = false;
    run(200000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        s_person = sim_ms() >= 100000 && sim_ms() < 110000;
        if (sim_ms() == 105000) on_at_105 = visible();
        if (sim_ms() == 165000) on_at_165 = visible();
    });
    CHECK(on_at_105 && on_at_165 && !g_sim.panel_on);

    // OLED unplugged for 10 s while on: re-initialized and on again.
    run(80000, [&] {
        s_person = true;
        g_sim.oled_present = !(sim_ms() >= 50000 && sim_ms() < 60000);
    });
    CHECK(visible());
    CHECK(sim_log_contains("re-initializing"));

    // Panel silently reset (power glitch, writes still acknowledged) while on.
    int64_t back_at = -1;
    run(80000, [&] {
        s_person = true;
        if (sim_ms() == 50000) {
            g_sim.panel_on = false;
            g_sim.panel_configured = false;
        }
        if (sim_ms() > 50000 && back_at < 0 && visible()) back_at = sim_ms();
    });
    CHECK(back_at > 50000 && back_at <= 61000);

    // Panel reset while off: settings sent again when it turns on.
    bool on_at_101 = false;
    run(110000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 80000) {
            g_sim.panel_on = false;
            g_sim.panel_configured = false;
        }
        s_person = sim_ms() >= 100000;
        if (sim_ms() == 101000) on_at_101 = visible();
    });
    CHECK(on_at_101);

    // Factory reset countdown turns the OLED on with nobody near.
    bool on_during_reset = false;
    run(70000, [&] {
        if (sim_ms() == 35000) local_ui_set_pm25(31);
        if (sim_ms() == 50000) local_ui_set_reset(LOCAL_UI_RESET_HOLD, 4);
        if (sim_ms() == 51000) on_during_reset = visible();
        if (sim_ms() == 52000) local_ui_set_reset(LOCAL_UI_RESET_NONE, 0);
    });
    CHECK(on_during_reset && !g_sim.panel_on);

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
