// See battery_gauge.h.

#include "battery_gauge.h"

#include <stddef.h>

// Rest voltage at ~60 uA, 20-25 degC. ESTIMATES from the Energizer, Panasonic and Maxell CR2032 discharge curves
// (light-load plateau ~2.95-3.0 V; the knee starts below ~2.9 V): calibrate with a real discharge (checklist L5).
struct Bound {
    uint16_t mv;
    uint8_t half_percent;
};
// The most a voltage allows: on the plateau anything, below it the knee
static constexpr Bound k_max[] = {
    {2900, 200}, {2850, 100}, {2800, 60}, {2700, 30}, {2600, 16}, {2500, 8}, {2400, 0},
};
// The least a voltage allows: a cell still on the plateau has not run out
static constexpr Bound k_min[] = {
    {2950, 40}, {2900, 30}, {2850, 10}, {2800, 0},
};

// A rest reading this high is a cell that has delivered at most a few mAh
static constexpr uint32_t k_new_cell_mv = 3020;
// Or this much above the voltage reported before the battery was taken out (a used cell recovers less in its rest)
static constexpr uint32_t k_new_cell_rise_mv = 60;

template <size_t N> static uint8_t interpolate(const Bound (&t)[N], uint32_t mv)
{
    if (mv >= t[0].mv) {
        return t[0].half_percent;
    }
    for (size_t i = 1; i < N; i++) {
        if (mv >= t[i].mv) {
            const Bound &hi = t[i - 1];
            const Bound &lo = t[i];
            return static_cast<uint8_t>(lo.half_percent +
                                        (mv - lo.mv) * (hi.half_percent - lo.half_percent) / (hi.mv - lo.mv));
        }
    }
    return t[N - 1].half_percent;
}

uint8_t battery_max_half_percent(uint32_t rest_mv)
{
    return interpolate(k_max, rest_mv);
}

uint8_t battery_min_half_percent(uint32_t rest_mv)
{
    return interpolate(k_min, rest_mv);
}

bool battery_is_new_cell(uint32_t rest_mv, uint32_t saved_mv)
{
    return rest_mv >= k_new_cell_mv || (saved_mv != 0 && rest_mv >= saved_mv + k_new_cell_rise_mv);
}

BatteryGauge::BatteryGauge(uint32_t usable_mah, uint32_t avg_ua)
    : m_usable_uas(static_cast<uint64_t>(usable_mah ? usable_mah : 1) * 3600 * 1000), m_avg_ua(avg_ua)
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
    // Rounded up: a fresh cell shows 100 % until a whole half-percent is used
    const uint64_t used_half = m_used_uas * 200 / m_usable_uas;
    uint32_t half = used_half >= 200 ? 0 : static_cast<uint32_t>(200 - used_half);
    if (rest_mv) {
        const uint32_t hi = battery_max_half_percent(rest_mv);
        const uint32_t lo = battery_min_half_percent(rest_mv);
        half = half > hi ? hi : half < lo ? lo : half;
    }
    return static_cast<uint8_t>(half);
}
