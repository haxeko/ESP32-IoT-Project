#pragma once

/* Relay outputs (menuconfig -> Fan Relays): one relay per motor speed tap and the oscillation (swing) motor relay.
 * Used by the fan task only (no locking). Also checks at build time that no two functions of the project share a GPIO
 * and that no unusable ESP32-C3 pin is chosen. */

#include <stdbool.h>
#include <stdint.h>

/** Configure the relay GPIOs with every relay open. Call once, before the fan task starts. */
void relays_init();

/** Close the relay of `speed` (0 = no speed relay) and set the oscillation relay. Break before make: one speed relay
 *  at a time, and CONFIG_FAN_RELAY_DEAD_TIME_MS between releasing one and closing another (also across a quick
 *  off -> on), so two motor taps are never on the mains together. The oscillation relay opens before the speed
 *  relay and closes only once a speed relay is closed; a speed change does not interrupt it. May block for the dead
 *  time.
 *  cancel_close (optional) is asked when the call had to wait for the dead time: true leaves the new speed relay
 *  open (a command came in meanwhile and the caller applies the relays again), so a relay is not closed only to be
 *  opened a moment later.
 *  Returns the speed relay closed now (0 = none). */
uint8_t relays_apply(uint8_t speed, bool swing, bool (*cancel_close)(void));
