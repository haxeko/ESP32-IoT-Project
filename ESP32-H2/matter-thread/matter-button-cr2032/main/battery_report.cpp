// See battery_report.h.

#include "battery_report.h"

#include <stddef.h>

// Coarse CR2032 state of charge from the rest voltage at ~30 uA, 25 degC, in Matter half-percent units.
// ASSUMPTION derived from the Energizer CR2032 discharge curves (flat ~2.9-3.0 V plateau, then a knee):
// calibrate with a real discharge (README, battery test). The level (OK/Warning/Critical) does not use it.
struct SocPoint {
    uint16_t mv;
    uint8_t half_percent;
};
static constexpr SocPoint k_soc[] = {
    {3000, 200}, {2950, 170}, {2900, 130}, {2850, 90}, {2800, 60}, {2750, 40}, {2700, 25}, {2600, 10}, {2400, 0},
};

uint8_t battery_soc_from_rest(uint32_t mv)
{
    if (mv >= k_soc[0].mv) {
        return k_soc[0].half_percent;
    }
    for (size_t i = 1; i < sizeof(k_soc) / sizeof(k_soc[0]); i++) {
        if (mv >= k_soc[i].mv) {
            const SocPoint &hi = k_soc[i - 1];
            const SocPoint &lo = k_soc[i];
            return static_cast<uint8_t>(lo.half_percent + (mv - lo.mv) * (hi.half_percent - lo.half_percent) /
                                                              (hi.mv - lo.mv));
        }
    }
    return 0;
}

void BatteryReport::Restore(uint32_t mv, uint8_t half_percent)
{
    m_mv = mv;
    m_half_percent = half_percent <= 200 ? half_percent : 200;
    m_lower = 0;
}

bool BatteryReport::OnReading(uint32_t mv, uint32_t now_s)
{
    const uint8_t half_percent = battery_soc_from_rest(mv);
    if (m_mv == 0 || half_percent >= m_half_percent) {
        const bool changed = mv != m_mv || half_percent != m_half_percent;
        m_mv = mv;
        m_half_percent = half_percent;
        m_lower = 0;
        return changed;
    }
    if (m_lower == 0) {
        m_lower_best_mv = mv;
        m_first_lower_s = now_s;
    } else if (mv > m_lower_best_mv) {
        m_lower_best_mv = mv;
    }
    if (m_lower < UINT8_MAX) {
        m_lower++;
    }
    if (m_lower < m_confirm || now_s - m_first_lower_s < m_span_s) {
        return false;
    }
    m_mv = m_lower_best_mv;
    m_half_percent = battery_soc_from_rest(m_lower_best_mv);
    m_lower = 0;
    return true;
}
