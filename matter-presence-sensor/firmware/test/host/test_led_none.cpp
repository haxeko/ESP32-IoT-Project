// Status LED (main/drivers/status_led.cpp) on a board without one (CONFIG_STATUS_LED_TYPE_NONE, c3-radar r45): every
// mode is accepted, no pin is driven.
#include "sim.h"

#include "status_led.cpp"

int main(int argc, char **argv)
{
    g_verbose = argc > 1;
    printf("status_led (none):\n");

    test_begin("no LED: init succeeds, modes and learning results are kept, no GPIO is driven");
    sim_reset();
    CHECK(status_led_init() == ESP_OK);
    status_led_set_mode(STATUS_LED_PAIRING);
    status_led_set_identify(true);
    status_led_set_learning(STATUS_LED_LEARNING_RECORDING);
    for (int i = 0; i < 50; i++) {
        g_sim.now_us += 100000;
        tick_cb(nullptr);
    }
    status_led_set_learning(STATUS_LED_LEARNING_DONE);
    tick_cb(nullptr);
    CHECK(s_mode == STATUS_LED_PAIRING && s_identify && s_ticks == 51);
    CHECK(g_sim.gpio_out.empty());

    return test_end();
}
