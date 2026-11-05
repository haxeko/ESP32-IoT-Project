#pragma once

/*
 * What the Power Source cluster reports from the CR2032 rest readings. Pure C++17 with no ESP-IDF dependency, so it
 * runs in the host test (test/host/test_battery_report.cpp); battery.cpp feeds it the rest readings.
 *
 *  - A reading that gives the same or a higher percentage is reported at once: the cell recovered, or a new one.
 *  - A lower one is reported only once `confirm_count` readings in a row are lower, and then the highest of them:
 *    one low reading (radio activity just before, a cold morning) does not lower the percentage.
 *  - Nothing reported yet (no saved values): the first reading is reported.
 * The reported voltage goes with the percentage.
 */

#include <stdint.h>

/** Coarse CR2032 state of charge from the rest voltage, in Matter half-percent units (0..200). */
uint8_t battery_soc_from_rest(uint32_t mv);

class BatteryReport {
public:
    explicit BatteryReport(uint8_t confirm_count) : m_confirm(confirm_count ? confirm_count : 1) {}

    /** The values reported before the battery was taken out (saved in flash). */
    void Restore(uint32_t mv, uint8_t half_percent);

    /** A rest reading. Returns true when the reported values changed. */
    bool OnReading(uint32_t mv);

    /** 0: nothing reported yet. */
    uint32_t ReportedMv() const { return m_mv; }
    uint8_t ReportedHalfPercent() const { return m_half_percent; }
    /** Lower readings in a row so far (a drop being confirmed), 0 when none. */
    uint8_t PendingLower() const { return m_lower; }

private:
    uint8_t m_confirm;
    uint32_t m_mv = 0;
    uint8_t m_half_percent = 0;
    uint8_t m_lower = 0;
    uint32_t m_lower_best_mv = 0;
};
