// See battery_gauge.h.

#include "battery_gauge.h"

#include <stddef.h>

// Rest voltage of a Li/MnO2 coin cell at ~60 uA, 20-25 degC. ESTIMATES from the Energizer, Panasonic and Maxell
// CR2032 / CR2450 discharge curves (light-load plateau ~2.95-3.0 V, knee from ~2.85 V): calibrate with a real
// discharge (checklist L5). On the plateau (>= k_plateau_mv) no cap; from k_knee_mv down the cap falls from the floor
// to 0 along k_knee (permille of the floor); in between it falls from 100 % to the floor.
static constexpr uint32_t k_plateau_mv = 2900;
static constexpr uint32_t k_knee_mv = 2850;
struct KneePoint {
    uint16_t mv;
    uint16_t permille; // of the floor
};
static constexpr KneePoint k_knee[] = {
    {2850, 1000}, {2800, 600}, {2750, 400}, {2700, 250}, {2600, 100}, {2500, 50}, {2400, 0},
};

// A rest reading this high is a cell that has delivered at most a few mAh (a used one falls below it within its first
// ~1-5 mAh); resetting the count of such a cell costs < 1 %
static constexpr uint32_t k_new_cell_mv = 3000;
// Or this much above the voltage reported before the battery was taken out: a used cell put back reads within
// ~10-25 mV of it (rest, temperature), a new one 40-150 mV higher. Immune to the ADC offset.
static constexpr uint32_t k_new_cell_rise_mv = 40;

static uint32_t lerp(uint32_t mv, uint32_t lo_mv, uint32_t hi_mv, uint32_t lo, uint32_t hi)
{
    return lo + (mv - lo_mv) * (hi - lo) / (hi_mv - lo_mv);
}

uint8_t battery_max_half_percent(uint32_t rest_mv, uint8_t floor_half_percent)
{
    const uint32_t floor = floor_half_percent > 200 ? 200 : floor_half_percent;
    if (rest_mv >= k_plateau_mv) {
        return 200;
    }
    if (rest_mv >= k_knee_mv) {
        return static_cast<uint8_t>(lerp(rest_mv, k_knee_mv, k_plateau_mv, floor, 200));
    }
    constexpr size_t n = sizeof(k_knee) / sizeof(k_knee[0]);
    for (size_t i = 1; i < n; i++) {
        if (rest_mv >= k_knee[i].mv) {
            const uint32_t permille =
                lerp(rest_mv, k_knee[i].mv, k_knee[i - 1].mv, k_knee[i].permille, k_knee[i - 1].permille);
            return static_cast<uint8_t>(floor * permille / 1000);
        }
    }
    return 0;
}

bool battery_is_new_cell(uint32_t rest_mv, uint32_t saved_mv)
{
    return rest_mv >= k_new_cell_mv || (saved_mv != 0 && rest_mv >= saved_mv + k_new_cell_rise_mv);
}

BatteryGauge::BatteryGauge(uint32_t nominal_mah, uint32_t avg_ua, uint8_t floor_half_percent)
    : m_nominal_uas(static_cast<uint64_t>(nominal_mah ? nominal_mah : 1) * 3600 * 1000), m_avg_ua(avg_ua),
      m_floor(floor_half_percent > 200 ? 200 : floor_half_percent)
{
}

void BatteryGauge::Restore(uint32_t used_mas)
{
    m_used_uas = static_cast<uint64_t>(used_mas) * 1000;
    m_remainder_uams = 0;
}

void BatteryGauge::NewCell()
{
    m_used_uas = 0;
    m_remainder_uams = 0;
}

void BatteryGauge::Elapse(uint64_t ms)
{
    const uint64_t uams = static_cast<uint64_t>(m_avg_ua) * ms + m_remainder_uams;
    m_used_uas += uams / 1000;
    m_remainder_uams = static_cast<uint32_t>(uams % 1000);
}

uint32_t BatteryGauge::UsedMas() const
{
    const uint64_t mas = m_used_uas / 1000;
    return mas > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(mas);
}

uint8_t BatteryGauge::HalfPercent(uint32_t rest_mv) const
{
    // Rounded up: a fresh cell shows 100 % until a whole half-percent is used; the count stops at the floor
    const uint64_t used_half = m_used_uas * 200 / m_nominal_uas;
    uint32_t half = used_half >= 200 ? 0 : static_cast<uint32_t>(200 - used_half);
    if (half < m_floor) {
        half = m_floor;
    }
    if (rest_mv) {
        const uint32_t cap = battery_max_half_percent(rest_mv, m_floor);
        if (half > cap) {
            half = cap;
        }
    }
    return static_cast<uint8_t>(half);
}
