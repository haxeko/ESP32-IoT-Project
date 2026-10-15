#pragma once

/* Fan state machine shared by Matter, the IR remote and the buttons. Pure C, no ESP-IDF dependency
 * (built and tested on the host by test/host/run.sh).
 *
 * The state holds the speed setting (Matter SpeedSetting, 0 = off), the oscillation setting (RockSetting) and the
 * natural wind setting (FanMode Auto / WindSetting). Natural wind is a mode of its own: every command that selects a
 * speed (SET_SPEED 1..3, SPEED_CYCLE, STEP to a running speed) leaves it, as does SET_NATURAL 0 / NATURAL_TOGGLE.
 * Off, On and Toggle keep it: turning the fan back on with the power button resumes natural wind. */

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
    uint8_t speed;      /**< speed setting: 0 = off, 1..FAN_SPEED_COUNT */
    uint8_t last_speed; /**< 1..FAN_SPEED_COUNT: speed of the next local "on" (remote power button, device button) */
    bool swing;         /**< oscillation setting; the swing relay runs only while the fan runs */
    bool natural;       /**< natural wind: while the fan runs, the fan task varies the speed (fan_natural_next());
                             speed is then the speed of the next manual setting */
} fan_state_t;

typedef enum {
    FAN_CMD_TOGGLE = 0,     /**< local power button: off -> on at last_speed, running -> off */
    FAN_CMD_ON,             /**< local "on" button: off -> on at last_speed, running: unchanged */
    FAN_CMD_OFF,            /**< -> off */
    FAN_CMD_SPEED_CYCLE,    /**< local speed button: off -> on at last_speed; running: 1 -> 2 -> 3 -> 1 */
    FAN_CMD_SET_SPEED,      /**< arg = 0..FAN_SPEED_COUNT (0 = off): Matter SpeedSetting, speed buttons */
    FAN_CMD_SWING_TOGGLE,   /**< oscillation on <-> off */
    FAN_CMD_SET_SWING,      /**< arg = 0 (off) / 1 (on): Matter RockSetting */
    FAN_CMD_STEP_UP,        /**< Matter Step, direction Increase (wrap, lowest_off) */
    FAN_CMD_STEP_DOWN,      /**< Matter Step, direction Decrease (wrap, lowest_off) */
    FAN_CMD_NATURAL_TOGGLE, /**< natural wind running -> manual; otherwise -> natural wind, on at last_speed when off */
    FAN_CMD_SET_NATURAL,    /**< arg = 1: natural wind, on at last_speed when off; arg = 0: manual, speed unchanged */
} fan_cmd_type_t;

typedef struct {
    uint8_t type; /**< fan_cmd_type_t */
    uint8_t arg;
    bool wrap;       /**< STEP: wrap around at the highest / lowest speed */
    bool lowest_off; /**< STEP: the lowest setting is Off (otherwise speed 1) */
} fan_cmd_t;

/** Speed to Matter percent: 0, 33, 66, 100 = floor(speed * 100 / SpeedMax), the Fan Control rule for PercentSetting
 *  after a SpeedSetting change and for PercentCurrent. */
uint8_t fan_speed_to_percent(uint8_t speed);

/** Apply a command. Returns true when the state changed. */
bool fan_state_apply(fan_state_t *state, const fan_cmd_t *cmd);

/** Clamp a state loaded from storage into valid ranges. */
void fan_state_sanitize(fan_state_t *state);

/** Whether the swing relay is closed for this state (oscillation on and the fan running). */
bool fan_state_swing_running(const fan_state_t *state);

/** Whether the fan task varies the speed (natural wind on and the fan running). */
bool fan_state_natural_running(const fan_state_t *state);

/** Natural wind: the next speed after `current` (0 = natural wind just started) and how long to hold it.
 *  The speed always changes: from 1 mostly up to 2, from 3 mostly down to 2, short gusts at 3; it starts at 1 or 2
 *  and the motor never stops. The hold time is random between min_s and max_s seconds (a gust at speed 3 at most
 *  halfway). rng: state of the random generator, any non-zero value to start with. */
uint8_t fan_natural_next(uint32_t *rng, uint8_t current, uint16_t min_s, uint16_t max_s, uint32_t *hold_ms);

#ifdef __cplusplus
}
#endif
