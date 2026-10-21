// Status LED as a WS2812 (main/drivers/status_led.cpp, CONFIG_STATUS_LED_TYPE_WS2812): colours of each state, the
// brightness, and a refresh only when the colour changes. The patterns themselves are tested by test_led.cpp.
#include "sim.h"

#include "status_led.cpp"

#include <string>

static void reset_led()
{
    sim_reset();
    s_mode = STATUS_LED_OFF;
    s_learning = STATUS_LED_LEARNING_NONE;
    s_learning_sets = 0;
    s_identify = false;
    s_ticks = 0;
    s_shown_learning = STATUS_LED_LEARNING_NONE;
    s_shown_learning_sets = 0;
    s_learning_ticks = 0;
    CHECK(status_led_init() == ESP_OK);
}

// One tick of 100 ms, then the colour shown as "r,g,b"
static std::string tick()
{
    g_sim.now_us += 100000;
    tick_cb(nullptr);
    return std::to_string(g_strip.shown_r) + "," + std::to_string(g_strip.shown_g) + "," +
           std::to_string(g_strip.shown_b);
}

// Brightness 20 of 255: full channels become 20, purple's 160 becomes 12
int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("status_led (WS2812, brightness %d):\n", CONFIG_STATUS_LED_BRIGHTNESS);

    test_begin("one LED on the configured pin, dark from the start");
    reset_led();
    CHECK(g_strip.gpio == CONFIG_STATUS_LED_GPIO && g_strip.refreshes == 1);
    CHECK(tick() == "0,0,0" && g_strip.refreshes == 1);

    test_begin("pairing: blue, 1 s on, 1 s off, sent only when it changes");
    reset_led();
    status_led_set_mode(STATUS_LED_PAIRING);
    for (int i = 0; i < 9; i++) {
        tick();
    }
    CHECK(tick() == "0,0,20");
    CHECK(g_strip.refreshes == 2);
    for (int i = 0; i < 9; i++) {
        tick();
    }
    CHECK(g_strip.refreshes == 2);
    CHECK(tick() == "0,0,0" && g_strip.refreshes == 3);

    test_begin("identify: white");
    reset_led();
    status_led_set_identify(true);
    CHECK(tick() == "20,20,20");

    test_begin("learning: cyan while waiting and recording, green when done, red when failed");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_WAITING);
    CHECK(tick() == "0,20,20");
    status_led_set_learning(STATUS_LED_LEARNING_RECORDING);
    for (int i = 0; i < 10; i++) {
        tick();
    }
    CHECK(tick() == "0,20,20");
    status_led_set_learning(STATUS_LED_LEARNING_DONE);
    CHECK(tick() == "0,20,0");
    status_led_set_learning(STATUS_LED_LEARNING_FAILED);
    CHECK(tick() == "20,0,0");

#if CONFIG_PRESENCE_BUTTON_LED_H2
    test_begin("factory thresholds, ESP32-H2 firmware: green for 10 s, then the mode");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_FACTORY);
    for (int i = 0; i < 100; i++) {
        CHECK(tick() == "0,20,0");
    }
    CHECK(tick() == "0,0,0");
#else
    test_begin("factory thresholds: purple");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_FACTORY);
    CHECK(tick() == "12,0,20");
    tick();
    CHECK(tick() == "0,0,0"); // 2.5 Hz blink
#endif

    return test_end();
}
