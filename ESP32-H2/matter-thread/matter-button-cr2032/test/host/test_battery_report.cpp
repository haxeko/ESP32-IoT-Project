// Host test of the reported rest voltage (main/battery_report.cpp): what BatVoltage and the percentage cap use for a
// series of rest readings with their times.
#include "battery_report.h"

#include <cstdio>
#include <utility>
#include <vector>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

static constexpr uint32_t k_hour_s = 3600;
static constexpr uint32_t k_day_s = 24 * k_hour_s;

// Feeds (mV, time) readings, returns the reported step after each one
static std::vector<int> feed(BatteryReport &r, std::vector<std::pair<uint32_t, uint32_t>> readings)
{
    std::vector<int> out;
    for (const auto &reading : readings) {
        r.OnReading(reading.first, reading.second);
        out.push_back(r.ReportedHalfPercent());
    }
    return out;
}

int main()
{
    check(battery_soc_from_rest(3300) == 200 && battery_soc_from_rest(3000) == 200, "scale: 3.0 V and above is the top");
    check(battery_soc_from_rest(2975) == 185, "scale: interpolated between table points");
    check(battery_soc_from_rest(2400) == 0 && battery_soc_from_rest(2000) == 0, "scale: 2.4 V and below is 0");

    {
        BatteryReport r(3, k_day_s);
        check(r.ReportedMv() == 0, "fresh: nothing reported");
        check(r.OnReading(2900, 0) && r.ReportedHalfPercent() == 130 && r.ReportedMv() == 2900,
              "fresh: the first reading is reported at once");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(3050, 200);
        check(r.ReportedHalfPercent() == 200 && r.ReportedMv() == 3050, "restore: saved values reported");
        check(!r.OnReading(2850, 0) && r.ReportedHalfPercent() == 200 && r.PendingLower() == 1,
              "one low reading: kept");
        check(r.OnReading(3010, k_hour_s) && r.ReportedHalfPercent() == 200 && r.ReportedMv() == 3010 &&
                  r.PendingLower() == 0,
              "recovered: same step, voltage updated, pending drop forgotten");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(3000, 200);
        const std::vector<int> got = feed(r, {{2860, 0}, {2900, 12 * k_hour_s}, {2880, k_day_s}});
        check(got == std::vector<int>({200, 200, 130}) && r.ReportedMv() == 2900,
              "three lower readings over 24 h: reported, as the highest of them");
        check(r.PendingLower() == 0, "after a confirmed drop: nothing pending");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(3000, 200);
        // Pressed all afternoon: four low readings within 3 h
        const std::vector<int> got =
            feed(r, {{2880, 0}, {2870, k_hour_s}, {2890, 2 * k_hour_s}, {2880, 3 * k_hour_s}});
        check(got == std::vector<int>({200, 200, 200, 200}) && r.PendingLower() == 4 &&
                  r.PendingSpan(3 * k_hour_s) == 3 * k_hour_s,
              "busy afternoon: four low readings within 3 h, not reported");
        check(!r.OnReading(3000, 12 * k_hour_s) && r.PendingLower() == 0 && r.ReportedMv() == 3000,
              "quiet night reading recovered: pending drop forgotten, nothing changed");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(3000, 200);
        // After a boot: the first reading and two follow-ups an hour apart, all low
        const std::vector<int> got = feed(r, {{2850, 0}, {2850, k_hour_s}, {2850, 2 * k_hour_s}, {2850, 14 * k_hour_s}});
        check(got == std::vector<int>({200, 200, 200, 200}), "follow-ups after a boot: 4 readings in 14 h, kept");
        r.OnReading(2850, k_day_s);
        check(r.ReportedHalfPercent() == 90 && r.ReportedMv() == 2850, "still low after 24 h: reported");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(3000, 200);
        const std::vector<int> got = feed(r, {{2900, 0}, {2900, 12 * k_hour_s}, {3000, k_day_s},
                                              {2900, 36 * k_hour_s}, {2900, 48 * k_hour_s}});
        check(got == std::vector<int>({200, 200, 200, 200, 200}) && r.PendingLower() == 2,
              "lower readings not in a row: not reported");
        r.OnReading(2900, 60 * k_hour_s);
        check(r.ReportedHalfPercent() == 130, "the third one in a row, 24 h after the first: reported");
    }
    {
        BatteryReport r(3, k_day_s);
        r.Restore(2850, 90);
        check(r.OnReading(3020, 0) && r.ReportedHalfPercent() == 200, "new cell (higher): reported at once");
    }
    {
        BatteryReport r(1, 0);
        r.Restore(3000, 200);
        check(r.OnReading(2800, 0) && r.ReportedHalfPercent() == 60, "confirm 1 reading, 0 h: every reading reported");
    }
    {
        BatteryReport r(0, 0);
        r.Restore(3000, 200);
        check(r.OnReading(2800, 0), "confirm count 0 acts as 1");
    }

    std::printf("battery report: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
