// Host test of the reported battery values (main/battery_report.cpp): what BatPercentRemaining / BatVoltage show
// for a series of rest readings.
#include "battery_report.h"

#include <cstdio>
#include <vector>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

// Feeds the readings, returns the reported half-percent after each one
static std::vector<int> feed(BatteryReport &r, std::vector<uint32_t> readings)
{
    std::vector<int> out;
    for (uint32_t mv : readings) {
        r.OnReading(mv);
        out.push_back(r.ReportedHalfPercent());
    }
    return out;
}

int main()
{
    check(battery_soc_from_rest(3300) == 200 && battery_soc_from_rest(3000) == 200, "soc: 3.0 V and above is 100 %");
    check(battery_soc_from_rest(2975) == 185, "soc: interpolated between table points");
    check(battery_soc_from_rest(2400) == 0 && battery_soc_from_rest(2000) == 0, "soc: 2.4 V and below is 0 %");

    {
        BatteryReport r(3);
        check(r.ReportedMv() == 0, "fresh: nothing reported");
        check(r.OnReading(2900) && r.ReportedHalfPercent() == 130 && r.ReportedMv() == 2900,
              "fresh: the first reading is reported at once");
    }
    {
        BatteryReport r(3);
        r.Restore(3050, 200);
        check(r.ReportedHalfPercent() == 200 && r.ReportedMv() == 3050, "restore: saved values reported");
        check(!r.OnReading(2850) && r.ReportedHalfPercent() == 200 && r.PendingLower() == 1,
              "one low reading after the restart: percentage kept");
        check(r.OnReading(3010) && r.ReportedHalfPercent() == 200 && r.ReportedMv() == 3010 && r.PendingLower() == 0,
              "recovered: same percentage, voltage updated, pending drop forgotten");
    }
    {
        BatteryReport r(3);
        r.Restore(3000, 200);
        const std::vector<int> got = feed(r, {2860, 2900, 2880});
        check(got == std::vector<int>({200, 200, 130}) && r.ReportedMv() == 2900,
              "three lower readings in a row: reported, as the highest of them");
        check(r.PendingLower() == 0, "after a confirmed drop: nothing pending");
    }
    {
        BatteryReport r(3);
        r.Restore(3000, 200);
        const std::vector<int> got = feed(r, {2900, 2900, 3000, 2900, 2900});
        check(got == std::vector<int>({200, 200, 200, 200, 200}) && r.PendingLower() == 2,
              "lower readings not in a row: not reported");
        r.OnReading(2900);
        check(r.ReportedHalfPercent() == 130, "the third one in a row: reported");
    }
    {
        BatteryReport r(3);
        r.Restore(2850, 90);
        check(r.OnReading(3020) && r.ReportedHalfPercent() == 200, "new cell (higher): reported at once");
    }
    {
        BatteryReport r(1);
        r.Restore(3000, 200);
        check(r.OnReading(2800) && r.ReportedHalfPercent() == 60, "confirm count 1: every reading reported");
    }
    {
        BatteryReport r(0);
        r.Restore(3000, 200);
        check(r.OnReading(2800), "confirm count 0 acts as 1");
    }

    std::printf("battery report: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
