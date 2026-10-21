// Status LED (main/drivers/status_led.cpp): patterns and priorities on the single-colour LED. Built twice: active
// low (SuperMini, default) and CONFIG_STATUS_LED_ACTIVE_HIGH=1.
#include "sim.h"

#include "status_led.cpp"

#include <string>

static const int kOn = CONFIG_STATUS_LED_ACTIVE_HIGH ? 1 : 0;

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
    s_level = -1;
    CHECK(status_led_init() == ESP_OK);
}

// n ticks of 100 ms: "#" lit, "." dark
static std::string run(int n)
{
    std::string s;
    for (int i = 0; i < n; i++) {
        g_sim.now_us += 100000;
        tick_cb(nullptr);
        s += g_sim.gpio_out[8] == kOn ? '#' : '.';
    }
    return s;
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("status_led (%s):\n", CONFIG_STATUS_LED_ACTIVE_HIGH ? "on when high" : "on when low");

    test_begin("dark from the start: the pin is set to off before the output is enabled");
    reset_led();
    CHECK(g_sim.gpio_out[8] == 1 - kOn && g_sim.timers.size() == 1 && g_sim.timers[0].period_us == 100000);
    CHECK(run(30) == std::string(30, '.'));

    test_begin("pairing: 1 s on, 1 s off");
    reset_led();
    status_led_set_mode(STATUS_LED_PAIRING);
    CHECK(run(40) == ".........##########..........##########.");
    status_led_set_mode(STATUS_LED_OFF);
    CHECK(run(20) == std::string(20, '.'));

    test_begin("identify: 5 Hz over everything");
    reset_led();
    status_led_set_mode(STATUS_LED_PAIRING);
    status_led_set_learning(STATUS_LED_LEARNING_WAITING);
    status_led_set_identify(true);
    CHECK(run(6) == "#.#.#.");
    status_led_set_identify(false);
    CHECK(run(3) == "###");

    test_begin("learning: on while waiting, then 1 s off / 1 s on while recording");
    reset_led();
    status_led_set_mode(STATUS_LED_PAIRING);
    status_led_set_learning(STATUS_LED_LEARNING_WAITING);
    CHECK(run(25) == std::string(25, '#'));
    status_led_set_learning(STATUS_LED_LEARNING_RECORDING);
    CHECK(run(40) == "..........##########..........##########");

    test_begin("done or failed: on for 10 s, then the mode");
    for (status_led_learning_t result : { STATUS_LED_LEARNING_DONE, STATUS_LED_LEARNING_FAILED }) {
        reset_led();
        status_led_set_learning(result);
        CHECK(run(100) == std::string(100, '#'));
        CHECK(run(20) == std::string(20, '.'));
    }

    test_begin("factory thresholds: 0.2 s on / 0.2 s off for 10 s, lit first");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_FACTORY);
    const std::string factory = run(100);
    CHECK(factory.substr(0, 8) == "##..##.." && factory.substr(96, 4) == "##..");
    CHECK(run(10) == std::string(10, '.'));

    test_begin("setting a result again restarts its 10 s");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_FAILED);
    run(80);
    status_led_set_learning(STATUS_LED_LEARNING_FAILED);
    CHECK(run(100) == std::string(100, '#'));
    CHECK(run(1) == ".");

    test_begin("cancelled learning: back to the mode at once");
    reset_led();
    status_led_set_learning(STATUS_LED_LEARNING_RECORDING);
    CHECK(run(15) == "..........#####");
    status_led_set_learning(STATUS_LED_LEARNING_NONE);
    CHECK(run(5) == ".....");
    reset_led();
    status_led_set_mode(STATUS_LED_PAIRING);
    status_led_set_learning(STATUS_LED_LEARNING_WAITING);
    CHECK(run(5) == "#####");
    status_led_set_learning(STATUS_LED_LEARNING_NONE);
    CHECK(run(15) == "....##########."); // pairing blink (ticks 6-20)

    return test_end();
}
