#pragma once

/*
 * The percentage BatPercentRemaining shows. Pure C++17 with no ESP-IDF dependency, so it runs in the host test
 * (test/host/test_battery_gauge.cpp); battery.cpp feeds it.
 *
 * A lithium coin cell (CR2032, CR2450) at this load (~60 uA) sits on a flat ~2.95-3.0 V plateau for most of its
 * life: the voltage cannot tell 95 % from 30 %. So the percentage has two parts:
 *  - 100 % down to the floor (20 % by default), evenly over the expected life: the charge used (running time x the
 *    modelled average current, Kconfig BATTERY_MODEL_AVG_UA) out of the nominal capacity. The count alone never
 *    goes below the floor: a cell that outlives the model stays at the floor.
 *  - below the floor, only the measured rest voltage: once the cell leaves its plateau (the knee, ~2.85 V) the
 *    voltage caps the percentage, at the floor at the start of the knee and 0 % at 2.4 V. The cap also applies when
 *    the knee comes before the count reached the floor (a device that draws more than modelled).
 * A new cell is recognised by its voltage after a power-on reset (battery_is_new_cell) and restarts the count.
 */

#include <stdint.h>

/** Highest percentage a reported rest voltage allows, in Matter half-percent units (0..200): 200 on the plateau,
 * floor_half_percent at the start of the knee, 0 at the end. */
uint8_t battery_max_half_percent(uint32_t rest_mv, uint8_t floor_half_percent);

/** A rest reading after a power-on reset that only a new cell gives: high in absolute terms, or well above the
 * voltage reported before the battery was taken out (saved_mv, 0 if none). */
bool battery_is_new_cell(uint32_t rest_mv, uint32_t saved_mv);

class BatteryGauge {
public:
    /** floor_half_percent: the lowest the count alone shows (40 = 20 %). */
    BatteryGauge(uint32_t nominal_mah, uint32_t avg_ua, uint8_t floor_half_percent);

    /** The charge used, as saved in flash (mA s). */
    void Restore(uint32_t used_mas);
    /** A new cell: nothing used yet. */
    void NewCell();
    /** Running time since the last call, at the modelled average current. */
    void Elapse(uint64_t ms);

    uint32_t UsedMas() const;
    /** From the charge used, capped by the reported rest voltage (0: no reading yet, no cap). */
    uint8_t HalfPercent(uint32_t rest_mv) const;

private:
    uint64_t m_nominal_uas;
    uint32_t m_avg_ua;
    uint8_t m_floor;
    uint64_t m_used_uas = 0;
    uint32_t m_remainder_uams = 0; // below 1 uA s, carried to the next Elapse()
};
