#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Step motor on a STEP/DIR/EN driver (TMC2209 in standalone mode, A4988, DRV8825...) with an end stop at the
 * bottom of the travel. A 20 kHz timer interrupt runs the motion planner (motion.h) and outputs the steps.
 *
 * Positions are in steps: 0 = closed (end stop), stepper_travel_steps() = fully open.
 * Call the functions from tasks, not from interrupts.
 */

/** Steps between closed and fully open (CONFIG_MOTOR_TRAVEL_MM). */
int32_t stepper_travel_steps(void);

/** GPIOs and timer; the driver output stays disabled. */
esp_err_t stepper_init(void);

/**
 * Find the closed position (blocking, a few seconds): leave the end stop if it is pressed, move down until it
 * is pressed, then touch it again slowly. Position 0 is where it is pressed.
 * @return ESP_OK; ESP_ERR_NOT_FOUND if the end stop was not reached within the travel (+25 %);
 *         ESP_ERR_INVALID_STATE if it stays pressed while moving up (wiring, or nothing moves).
 */
esp_err_t stepper_home(void);

/** Move to a position (clamped to 0..travel). Returns at once; the motion runs in the interrupt. */
void stepper_move_to(int32_t pos);

/** Current position in steps (updated by the interrupt at every step). */
int32_t stepper_position(void);

/** Moving or not at rest on the target. */
bool stepper_busy(void);

/**
 * Housekeeping, call every few tens of ms: stops the timer interrupt once at rest, and turns the driver off
 * after CONFIG_MOTOR_IDLE_OFF_MS at rest (when enabled).
 * @return true if the end stop re-zeroed the position since the last call (steps were lost).
 */
bool stepper_poll(void);

/** Driver output off (motor free, no holding torque) and motion stopped. */
void stepper_disable(void);

#ifdef __cplusplus
}
#endif
