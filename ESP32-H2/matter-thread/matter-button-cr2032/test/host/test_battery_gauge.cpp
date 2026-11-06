// Host test of the reported percentage (main/battery_gauge.cpp): charge counted from the running time, bounded by the
// rest voltage.
#include "battery_gauge.h"

#include <cstdio>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

static constexpr uint64_t k_hour_ms = 3600ULL * 1000;

int main()
{
    // 176 mAh usable at 60 uA: 1 half-percent = 0.88 mAh = 14.7 h
    {
        BatteryGauge g(176, 60);
        check(g.HalfPercent(0) == 200 && g.HalfPercent(3050) == 200, "new cell: 100 %");
        g.Elapse(6 * k_hour_ms);
        check(g.HalfPercent(2957) == 200, "after 6 h on the plateau: still 100 %");
        check(g.UsedMas() == 6 * 3600 * 60 / 1000, "6 h x 60 uA counted");
        g.Elapse(24 * k_hour_ms);
        check(g.HalfPercent(2957) == 198, "after 30 h: 99 %");
        g.Elapse(60ULL * 24 * k_hour_ms);
        check(g.HalfPercent(2950) == 100, "after ~61 days (88.2 mAh): 50 %");
    }
    {
        BatteryGauge g(176, 60);
        for (int i = 0; i < 3600; i++) {
            g.Elapse(1000); // 1 s steps add up without rounding loss
        }
        check(g.UsedMas() == 216, "1 h in 1 s steps: 216 mA s");
    }
    {
        BatteryGauge g(176, 60);
        check(g.HalfPercent(2850) == 100 && g.HalfPercent(2700) == 30 && g.HalfPercent(2400) == 0,
              "model under-counts: the knee caps the percentage");
        g.Elapse(200ULL * 24 * k_hour_ms); // far beyond the usable capacity
        check(g.HalfPercent(0) == 0, "no reading: the count alone");
        check(g.HalfPercent(2960) == 40, "model over-counts on the plateau: floor 20 %");
        check(g.HalfPercent(2780) == 0, "model over-counts, below the plateau: no floor");
    }
    {
        BatteryGauge g(176, 60);
        g.Restore(100000);
        check(g.UsedMas() == 100000 && g.HalfPercent(2960) == 200 - 31, "restored count");
        g.NewCell();
        check(g.UsedMas() == 0 && g.HalfPercent(2960) == 200, "new cell: count restarted");
    }
    check(battery_max_half_percent(3100) == 200 && battery_max_half_percent(2825) == 80 &&
              battery_max_half_percent(2000) == 0,
          "upper bound: interpolated, clamped");
    check(battery_min_half_percent(3100) == 40 && battery_min_half_percent(2700) == 0, "lower bound");
    check(battery_is_new_cell(3020, 2960) && battery_is_new_cell(3010, 2900) && !battery_is_new_cell(3000, 2960) &&
              !battery_is_new_cell(2990, 0),
          "new cell: >= 3020 mV, or 60 mV above the saved voltage");

    std::printf("battery gauge: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
