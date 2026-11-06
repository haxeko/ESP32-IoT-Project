#pragma once

/*
 * The percentage BatPercentRemaining shows, from the charge used rather than from the voltage alone. Pure C++17 with
 * no ESP-IDF dependency, so it runs in the host test (test/host/test_battery_gauge.cpp); battery.cpp feeds it.
 *
 * A CR2032 at this load (~60 uA) sits on a flat ~2.95-3.0 V plateau for most of its life: the voltage cannot tell
 * 95 % from 40 %, and a new cell falls ~0.1 V from its open-circuit voltage within the first few mAh. So:
 *  - the charge used is counted from the running time and a modelled average current (Kconfig
 *    BATTERY_MODEL_AVG_UA, measured with checklist F5), and gives the percentage of the usable capacity;
 *  - the reported rest voltage only bounds it: below the plateau a percentage the voltage cannot have is capped
 *    (end of life shows even if the model under-counts), and on the plateau a percentage the voltage contradicts is
 *    raised to a floor (a model that over-counts cannot show an empty cell).
 * A new cell is recognised by its voltage after a power-on reset (battery_is_new_cell) and restarts the count.
 */

#include <stdint.h>

/** Highest / lowest percentage a reported rest voltage allows, in Matter half-percent units (0..200). */
uint8_t battery_max_half_percent(uint32_t rest_mv);
uint8_t battery_min_half_percent(uint32_t rest_mv);

/** A rest reading after a power-on reset that only a new cell gives: high in absolute terms, or well above the
 * voltage reported before the battery was taken out (saved_mv, 0 if none). */
bool battery_is_new_cell(uint32_t rest_mv, uint32_t saved_mv);

class BatteryGauge {
public:
    BatteryGauge(uint32_t usable_mah, uint32_t avg_ua);

    /** The charge used, as saved in flash (mA s). */
    void Restore(uint32_t used_mas);
    /** A new cell: nothing used yet. */
    void NewCell();
    /** Running time since the last call, at the modelled average current. */
    void Elapse(uint64_t ms);

    uint32_t UsedMas() const;
    /** From the charge used, bounded by the reported rest voltage (0: no reading yet, no bound). */
    uint8_t HalfPercent(uint32_t rest_mv) const;

private:
    uint64_t m_usable_uas;
    uint32_t m_avg_ua;
    uint64_t m_used_uas = 0;
    uint32_t m_remainder_uams = 0; // below 1 uA s, carried to the next Elapse()
};
