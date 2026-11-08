#pragma once

/*
 * Factory reset without a button: the battery inserted CONFIG_POWER_CYCLE_RESET_COUNT times in a row, each time taken
 * out again within CONFIG_POWER_CYCLE_WINDOW_S of the insertion. Pure logic with no ESP-IDF dependency (host test
 * test/host/test_power_cycle.cpp); app_main.cpp keeps the count in NVS and clears it once a boot has run for the window.
 *
 * At each boot:
 *  - a power-on reset (battery inserted) adds one; reaching reset_count asks for the reset and clears the count
 *  - any other reset (software, Deep-sleep wakeup, brownout, panic, watchdog) clears it: the brownout loop of a weak
 *    cell, or a boot guard Deep-sleep, never counts towards a reset
 * reset_count 0: off (a stored count is cleared).
 */

#include <stdint.h>

struct PowerCycleResult {
    uint8_t count; // to store
    bool reset;    // factory reset requested
    bool write;    // count differs from the stored one
};

PowerCycleResult power_cycle_on_boot(uint8_t stored, bool power_on_reset, uint8_t reset_count);
