#pragma once

/*
 * Which rest voltage the Power Source cluster reports (BatVoltage, the battery level, and the cap of the percentage in
 * battery_gauge.h). Pure C++17 with no ESP-IDF dependency, so it runs in the host test
 * (test/host/test_battery_report.cpp); battery.cpp feeds it the rest readings.
 *
 * Button presses, a Thread attach or a cold night lower a coin cell's voltage for minutes to hours; ageing lowers it
 * over weeks. So:
 *  - A reading on the same or a higher step is reported at once: the cell recovered, or a new one.
 *  - A lower one is reported only once `confirm_count` readings in a row are lower AND they span at least
 *    `confirm_span_s` (24 h by default: the night readings, taken after hours without a press, are in it), and then
 *    as the highest of them. A busy afternoon, even with several low readings, does not lower anything.
 *  - Nothing reported yet (no saved values): the first reading is reported.
 */

#include <stdint.h>

/** Coarse voltage scale (half-percent units, 0..200) on which readings are compared: a reading on the same or a higher
 * step counts as "same or higher". Not the reported percentage (battery_gauge.h). */
uint8_t battery_soc_from_rest(uint32_t mv);

class BatteryReport {
public:
    BatteryReport(uint8_t confirm_count, uint32_t confirm_span_s)
        : m_confirm(confirm_count ? confirm_count : 1), m_span_s(confirm_span_s)
    {
    }

    /** The values reported before the battery was taken out (saved in flash). */
    void Restore(uint32_t mv, uint8_t half_percent);

    /** A rest reading at now_s (any monotonic clock in seconds). Returns true when the reported values changed. */
    bool OnReading(uint32_t mv, uint32_t now_s);

    /** 0: nothing reported yet. */
    uint32_t ReportedMv() const { return m_mv; }
    uint8_t ReportedHalfPercent() const { return m_half_percent; }
    /** Lower readings in a row so far (a drop being confirmed), 0 when none. */
    uint8_t PendingLower() const { return m_lower; }
    /** Seconds since the first of them (0 when none). */
    uint32_t PendingSpan(uint32_t now_s) const { return m_lower ? now_s - m_first_lower_s : 0; }

private:
    uint8_t m_confirm;
    uint32_t m_span_s;
    uint32_t m_mv = 0;
    uint8_t m_half_percent = 0;
    uint8_t m_lower = 0;
    uint32_t m_lower_best_mv = 0;
    uint32_t m_first_lower_s = 0;
};
