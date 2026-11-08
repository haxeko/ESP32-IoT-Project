// See power_cycle.h. No ESP-IDF includes: also built by test/host/run.sh.

#include "power_cycle.h"

PowerCycleResult power_cycle_on_boot(uint8_t stored, bool power_on_reset, uint8_t reset_count)
{
    if (reset_count == 0 || !power_on_reset) {
        return PowerCycleResult{0, false, stored != 0};
    }
    const uint8_t count = stored < UINT8_MAX ? static_cast<uint8_t>(stored + 1) : UINT8_MAX;
    if (count >= reset_count) {
        return PowerCycleResult{0, true, stored != 0};
    }
    return PowerCycleResult{count, false, true};
}
