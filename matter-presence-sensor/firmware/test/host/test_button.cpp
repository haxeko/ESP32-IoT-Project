// BOOT button (main/drivers/user_button.cpp): clicks, long press, debounce, polled every 20 ms.
#include "sim.h"

#include "user_button.cpp"

static int s_double, s_triple, s_quadruple, s_long;

static void reset_button()
{
    sim_reset();
    s_now_ms = 0;
    s_pressed = false;
    s_unstable = 0;
    s_press_start_ms = 0;
    s_release_ms = 0;
    s_clicks = 0;
    s_hint_logged = false;
    s_long_fired = false;
    s_double = s_triple = s_quadruple = s_long = 0;
    const user_button_config_t config = {
        .on_double_click = []() { s_double++; },
        .on_triple_click = []() { s_triple++; },
        .on_quadruple_click = []() { s_quadruple++; },
        .on_long_press = []() { s_long++; },
        .long_press_ms = 5000,
    };
    CHECK(user_button_init(9, &config) == ESP_OK);
}

// Holds the pin at level (0 = pressed) for ms, polled every 20 ms like the esp_timer.
static void hold(int level, uint32_t ms)
{
    g_sim.gpio_in[9] = level;
    for (uint32_t t = 0; t < ms; t += 20) {
        g_sim.now_us += 20000;
        poll_cb(nullptr);
    }
}

static void clicks(int n, uint32_t press_ms = 100, uint32_t gap_ms = 200)
{
    for (int i = 0; i < n; i++) {
        hold(0, press_ms);
        hold(1, i + 1 < n ? gap_ms : 0);
    }
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("user_button:\n");

    test_begin("double click fires once the button stays released for more than 400 ms");
    reset_button();
    clicks(2);
    hold(1, 400);
    CHECK(s_double == 0);
    hold(1, 100);
    CHECK(s_double == 1 && s_triple == 0 && sim_log_contains("Double click"));

    test_begin("three and four clicks");
    reset_button();
    clicks(3);
    hold(1, 1000);
    CHECK(s_triple == 1 && s_double == 0 && s_quadruple == 0);
    clicks(4);
    hold(1, 1000);
    CHECK(s_quadruple == 1 && s_triple == 1);

    test_begin("five clicks or more do nothing");
    reset_button();
    clicks(5);
    hold(1, 1000);
    clicks(7);
    hold(1, 1000);
    CHECK(s_double + s_triple + s_quadruple + s_long == 0);
    CHECK(sim_log_contains("5 clicks, ignored") && sim_log_contains("7 clicks, ignored"));

    test_begin("a single click does nothing; clicks 500 ms apart are separate");
    reset_button();
    clicks(1);
    hold(1, 1000);
    clicks(1);
    hold(1, 500);
    clicks(1);
    hold(1, 1000);
    CHECK(s_double + s_triple + s_quadruple + s_long == 0);

    test_begin("hold 5 s: factory reset while still held, hint after 1 s, once");
    reset_button();
    hold(0, 1100);
    CHECK(sim_log_contains("Button held, keep holding for 5 s in total to factory reset") && s_long == 0);
    hold(0, 3800);
    CHECK(s_long == 0);
    hold(0, 200);
    CHECK(s_long == 1);
    hold(0, 3000);
    hold(1, 1000);
    CHECK(s_long == 1 && s_double + s_triple + s_quadruple == 0);

    test_begin("a press longer than 500 ms cancels the clicks before it");
    reset_button();
    clicks(2);
    hold(1, 200);
    hold(0, 800);
    hold(1, 1000);
    CHECK(s_double + s_triple + s_quadruple + s_long == 0);
    clicks(2); // a new sequence works again
    hold(1, 1000);
    CHECK(s_double == 1);

    test_begin("debounce: a level must hold for two samples (40 ms)");
    reset_button();
    for (int i = 0; i < 3; i++) { // 20 ms glitches: no click
        hold(0, 20);
        hold(1, 200);
    }
    CHECK(s_pressed == false);
    hold(1, 1000);
    CHECK(s_double + s_triple + s_quadruple == 0);
    clicks(2, 40, 100); // shortest real clicks
    hold(1, 1000);
    CHECK(s_double == 1);

    test_begin("no button (-1): nothing set up");
    reset_button();
    const size_t timers = g_sim.timers.size();
    const user_button_config_t none = {};
    CHECK(user_button_init(-1, &none) == ESP_OK && g_sim.timers.size() == timers);

    return test_end();
}
