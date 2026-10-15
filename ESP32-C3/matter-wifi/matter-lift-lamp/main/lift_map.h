#pragma once

#include <stdint.h>

/*
 * Height <-> LEDs of the lift lamp (pure functions, tested on a computer).
 * The strip runs along the travel: `leds` LEDs over `travel` steps, LED 1 at the bottom. At height `pos` the
 * LEDs below it are exposed; a LED counts as exposed once at least half of its height is.
 * lift_exposed_leds(lift_steps_for_leds(n)) == n for every n.
 */

/** Height (steps) that exposes `count` LEDs. */
static inline int32_t lift_steps_for_leds(uint16_t count, uint16_t leds, int32_t travel)
{
    if (leds == 0 || travel <= 0) {
        return 0;
    }
    if (count > leds) {
        count = leds;
    }
    return (int32_t)(((int64_t)count * travel + leds / 2) / leds);
}

/** LEDs exposed at height `pos` (steps). */
static inline uint16_t lift_exposed_leds(int32_t pos, uint16_t leds, int32_t travel)
{
    if (pos <= 0 || travel <= 0) {
        return 0;
    }
    if (pos >= travel) {
        return leds;
    }
    return (uint16_t)(((int64_t)pos * leds + travel / 2) / travel);
}
