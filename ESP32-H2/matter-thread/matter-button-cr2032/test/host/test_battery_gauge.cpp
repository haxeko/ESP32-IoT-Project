// Host test of the reported percentage (main/battery_gauge.cpp): 100 % down to the floor from the charge counted over
// the running time, below the floor only from the rest voltage.
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

static constexpr uint64_t k_day_ms = 24ULL * 3600 * 1000;

int main()
{
    // CR2450 620 mAh at 60 uA, floor 20 %: 1 % = 6.2 mAh = 4.3 days; the floor after 496 mAh = 344 days
    {
        BatteryGauge g(620, 60, 40);
        check(g.HalfPercent(0) == 200 && g.HalfPercent(3050) == 200, "new cell: 100 %");
        g.Elapse(2 * k_day_ms);
        check(g.HalfPercent(2960) == 200, "after 2 days: still 100 %");
        g.Elapse(84 * k_day_ms);
        check(g.HalfPercent(2950) == 161, "after 86 days (123.8 mAh): 80.5 %");
        g.Elapse(86 * k_day_ms);
        check(g.HalfPercent(2940) == 121, "after 172 days (247.7 mAh): 60.5 %");
        g.Elapse(172 * k_day_ms);
        check(g.HalfPercent(2930) == 41, "after 344 days (495.4 mAh): 20.5 %");
        g.Elapse(k_day_ms);
        check(g.HalfPercent(2930) == 40, "after 345 days: 20 %");
        g.Elapse(100 * k_day_ms);
        check(g.HalfPercent(2920) == 40 && g.HalfPercent(0) == 40, "cell outlives the model: stays at 20 %");
        check(g.HalfPercent(2800) == 24 && g.HalfPercent(2700) == 10 && g.HalfPercent(2400) == 0,
              "below 20 %: from the voltage only");
    }
    {
        BatteryGauge g(620, 60, 40);
        g.Elapse(30 * k_day_ms);
        check(g.HalfPercent(2850) == 40, "knee before the count: capped at 20 %");
        check(g.HalfPercent(2875) == 120, "between plateau and knee: 100 % -> 20 % by voltage");
        check(g.HalfPercent(2600) == 4, "deep in the knee: 2 %");
    }
    {
        BatteryGauge g(220, 60, 40); // CR2032: the floor after 176 mAh = 122 days
        g.Elapse(122 * k_day_ms);
        check(g.HalfPercent(2930) == 41, "CR2032 after 122 days: ~20 %");
    }
    {
        BatteryGauge g(620, 60, 40);
        for (int i = 0; i < 3600; i++) {
            g.Elapse(1000); // 1 s steps add up without rounding loss
        }
        check(g.UsedMas() == 216, "1 h in 1 s steps: 216 mA s");
        g.Restore(1116000); // 310 mAh
        check(g.UsedMas() == 1116000 && g.HalfPercent(2940) == 100, "restored count (310 mAh): 50 %");
        g.NewCell();
        check(g.UsedMas() == 0 && g.HalfPercent(2960) == 200, "new cell: count restarted");
    }
    {
        BatteryGauge g(620, 60, 0);
        g.Elapse(500 * k_day_ms);
        check(g.HalfPercent(0) == 0, "floor 0: the count alone reaches 0 %");
    }
    check(battery_max_half_percent(3100, 40) == 200 && battery_max_half_percent(2900, 40) == 200 &&
              battery_max_half_percent(2850, 40) == 40 && battery_max_half_percent(2000, 40) == 0,
          "voltage cap: plateau, knee, empty");
    check(battery_is_new_cell(3000, 2980) && battery_is_new_cell(2995, 2950) && !battery_is_new_cell(2990, 2960) &&
              !battery_is_new_cell(2990, 0),
          "new cell: >= 3000 mV, or 40 mV above the saved voltage");

    std::printf("battery gauge: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
