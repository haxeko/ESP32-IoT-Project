#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The lamp's moving upper part, driven by the step motor (stepper.h), synchronised with the LED strip (led_bar.h):
 *  - Matter brightness = height: level L opens the lamp to expose led_bar_count_for_level(L) LEDs; Off closes it;
 *  - while it moves, exactly the LEDs it has exposed are lit (the hidden ones stay off).
 * At boot the position is unknown: the lamp closes onto the end stop (homing), then goes to the stored state.
 * Without a motor (CONFIG_MOTOR_ENABLED off) or if homing fails, the LEDs follow the level as on a plain light.
 *
 * Call after led_bar_init(). Starts a task; returns at once.
 */
esp_err_t lift_init(void);

#ifdef __cplusplus
}
#endif
