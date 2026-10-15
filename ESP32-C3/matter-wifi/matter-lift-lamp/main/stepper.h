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
 * is pressed, move up 3 mm (it must release), then touch it again slowly. Position 0 is where it is pressed.
 * Also the way back after STEPPER_EVENT_FAULT or STEPPER_EVENT_NOT_CLOSED. The search down covers 125 % of the
 * travel; after STEPPER_EVENT_NOT_CLOSED only the height the sleeve can be at: every step it moved up since the
 * end stop last set position 0 (it only rises when the motor steps up), + 3 mm. When the end stop is not reached,
 * the sleeve backs up 10 mm (rather than keep pressing on whatever is in the way) and holds there.
 * @return ESP_OK; ESP_ERR_NOT_FOUND if the end stop was not reached within the search;
 *         ESP_ERR_INVALID_STATE if it stays pressed while moving up (broken NC wire, the motor does not move or
 *         cannot lift the sleeve).
 */
esp_err_t stepper_home(void);

/**
 * Move to a position (clamped to 0..travel). Returns at once; the motion runs in the interrupt.
 * Closing (0) checks the position: the last 10 mm run at the homing speed and end on the end stop (position 0
 * corrected there); if it is not found within 5 mm below 0, the sleeve backs up 10 mm and holds there, not ready
 * (STEPPER_EVENT_NOT_CLOSED).
 * @return false when refused, call again later: not ready (not homed, after a fault, or a closing that missed the
 *         end stop: stepper_home() first), or an opening while the closing checks the end stop (from 10 mm down
 *         to the end stop or 5 mm below 0, then it is accepted).
 */
bool stepper_move_to(int32_t pos);

/** Homed, no fault and no failed closing since: stepper_move_to() moves the motor. */
bool stepper_ready(void);

/** Current position in steps (updated by the interrupt at every step). */
int32_t stepper_position(void);

/** Moving or not at rest on the target. */
bool stepper_busy(void);

typedef enum {
    STEPPER_EVENT_NONE,
    /** Closing met the end stop more than 1 mm from 0 (within 3 mm): steps were lost, position 0 corrected */
    STEPPER_EVENT_REZEROED,
    /**
     * The end stop read pressed where the lamp cannot be closed (more than 3 mm above 0, or it did not release
     * when opening): broken NC wire, noise, or many lost steps. The motor stopped where it was (holding);
     * call stepper_home(), which tells a real press (re-homes) from a stuck end stop (fails).
     */
    STEPPER_EVENT_FAULT,
    /**
     * Closing did not find the end stop 5 mm below 0: something holds the sleeve up (or steps were lost going
     * down). It backed up 10 mm and rests there (driver as at any rest). Call stepper_home() before moving again.
     */
    STEPPER_EVENT_NOT_CLOSED,
} stepper_event_t;

/**
 * Housekeeping, call every 20 ms while stepper_busy(), every few hundred ms otherwise: runs the closing phases,
 * stops the timer interrupt once at rest, turns the driver off after CONFIG_MOTOR_IDLE_OFF_MS at rest (when set).
 * @return what happened since the last call.
 */
stepper_event_t stepper_poll(void);

/**
 * Stop for good (until stepper_home()): motion and timer interrupt stopped, not ready. The driver keeps its state:
 * holding where it is when CONFIG_MOTOR_IDLE_OFF_MS is 0 (a lead screw that is not self-locking), else off.
 */
void stepper_stop(void);

/** Driver output off (motor free, no holding torque) and motion stopped. */
void stepper_disable(void);

#ifdef __cplusplus
}
#endif
