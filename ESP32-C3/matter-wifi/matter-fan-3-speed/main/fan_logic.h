#pragma once

/* Fan state machine shared by Matter, the IR remote and the buttons. Pure C, no ESP-IDF dependency
 * (built and tested on the host by test/host/run.sh). */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of fan speeds (motor taps, one relay each). Matter SpeedMax. */
#define FAN_SPEED_COUNT 3

/** Speed used by the first "on" when nothing is stored yet */
#define FAN_DEFAULT_SPEED 1

typedef struct {
    uint8_t speed;      /**< 0 = off, 1..FAN_SPEED_COUNT */
    uint8_t last_speed; /**< 1..FAN_SPEED_COUNT: speed of the next "on" without a speed (power button, FanMode On) */
    bool swing;         /**< oscillation setting; the swing relay runs only while the fan runs */
} fan_state_t;

typedef enum {
    FAN_CMD_TOGGLE = 0,   /**< off -> on at last_speed, running -> off */
    FAN_CMD_ON,           /**< off -> on at last_speed, running: unchanged */
    FAN_CMD_OFF,          /**< -> off */
    FAN_CMD_SPEED_CYCLE,  /**< off -> on at last_speed; running: 1 -> 2 -> 3 -> 1 */
    FAN_CMD_SET_SPEED,    /**< arg = 0..FAN_SPEED_COUNT (0 = off) */
    FAN_CMD_SWING_TOGGLE, /**< oscillation on <-> off */
    FAN_CMD_SET_SWING,    /**< arg = 0 (off) / 1 (on) */
    FAN_CMD_STEP_UP,      /**< Matter Step, direction Increase (wrap, lowest_off) */
    FAN_CMD_STEP_DOWN,    /**< Matter Step, direction Decrease (wrap, lowest_off) */
} fan_cmd_type_t;

typedef struct {
    uint8_t type; /**< fan_cmd_type_t */
    uint8_t arg;
    bool wrap;       /**< STEP: wrap around at the highest / lowest speed */
    bool lowest_off; /**< STEP: the lowest setting is Off (otherwise speed 1) */
} fan_cmd_t;

/** Matter percent (PercentSetting) to speed, nearest of 33 / 66 / 100 %:
 *  0 -> 0 (off), 1..49 -> 1, 50..83 -> 2, 84..100 -> 3. Values above 100 count as 100. */
uint8_t fan_percent_to_speed(uint8_t percent);

/** Speed to Matter percent: 0, 33, 66, 100 (= floor(speed * 100 / SpeedMax), the Matter SpeedSetting rule). */
uint8_t fan_speed_to_percent(uint8_t speed);

/** Apply a command. Returns true when the state changed. */
bool fan_state_apply(fan_state_t *state, const fan_cmd_t *cmd);

/** Clamp a state loaded from storage into valid ranges. */
void fan_state_sanitize(fan_state_t *state);

/** Whether the swing relay is closed for this state (oscillation on and the fan running). */
bool fan_state_swing_running(const fan_state_t *state);

#ifdef __cplusplus
}
#endif
