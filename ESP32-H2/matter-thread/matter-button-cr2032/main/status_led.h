#pragma once

/*
 * Optional status LED (CONFIG_STATUS_LED_GPIO, -1 = none). Patterns are short flashes so the LED costs little:
 * the chip may Light-sleep between flashes (esp_timer wakes it) and a PM lock is held only while the LED is lit,
 * because the GPIO output is not kept while the digital peripherals are powered down.
 *
 * Persistent modes (highest priority shown): reset hold > pairing hold > boot reset wait > identify > pairing window.
 * One-shot flashes override them for their duration. Every function is safe from any task, not from ISRs.
 */

#include <stdbool.h>

enum class LedMode : unsigned {
    PairingWindow, // commissioning window open: 40 ms flash every 3 s
    Identify,      // Matter Identify: 200 ms on / 300 ms off
    BootResetWait, // button held at power-up: fast blink until the reset decision
    HoldPairing,   // button held past the pairing threshold: 100 / 400 ms
    HoldReset,     // button held past the factory reset threshold: 50 / 100 ms
    Count,
};

enum class LedFlash : unsigned {
    Ack,          // 20 ms: gesture recognised (CONFIG_STATUS_LED_PRESS_FEEDBACK)
    LowBattery,   // 2 x 40 ms: battery Warning/Critical, after a gesture
    ResetConfirm, // 3 x 150 ms: factory reset accepted
    Count,
};

void status_led_init(void);
void status_led_set(LedMode mode, bool on);
void status_led_flash(LedFlash flash);
/** Duration of a one-shot flash pattern, to wait for it to be seen (e.g. before a factory reset). */
unsigned status_led_flash_duration_ms(LedFlash flash);
