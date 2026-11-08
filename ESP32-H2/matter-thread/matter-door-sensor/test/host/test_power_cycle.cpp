// Host test of the factory reset by battery insertions (main/power_cycle.cpp).
#include "power_cycle.h"

#include <cstdio>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

int main()
{
    // 5 insertions in a row
    uint8_t stored = 0;
    bool reset = false;
    for (int i = 1; i <= 5; i++) {
        const PowerCycleResult r = power_cycle_on_boot(stored, true, 5);
        if (i < 5) {
            check(r.count == i && !r.reset && r.write, "insertion counted");
        }
        stored = r.count;
        reset = r.reset;
    }
    check(reset && stored == 0, "5th insertion: factory reset, count cleared");

    PowerCycleResult r = power_cycle_on_boot(3, false, 5);
    check(r.count == 0 && !r.reset && r.write, "a brownout / Deep-sleep / software reset in between: from 0 again");
    r = power_cycle_on_boot(0, false, 5);
    check(r.count == 0 && !r.write, "normal boot with nothing stored: no NVS write");
    r = power_cycle_on_boot(0, true, 5);
    check(r.count == 1 && !r.reset, "one insertion (the normal case): count 1, cleared by the window timer");
    r = power_cycle_on_boot(2, true, 0);
    check(r.count == 0 && !r.reset && r.write, "reset_count 0: off, stored count cleared");
    r = power_cycle_on_boot(255, true, 5);
    check(r.reset, "saturated count: reset");
    r = power_cycle_on_boot(0, true, 1);
    check(r.reset && r.count == 0 && !r.write, "reset_count 1: every insertion resets");

    std::printf("power cycle: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
