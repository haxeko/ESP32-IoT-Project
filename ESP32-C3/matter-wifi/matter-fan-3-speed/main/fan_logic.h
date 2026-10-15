#pragma once

/* Fan state machine shared by Matter, the IR remote and the buttons. Pure C, no ESP-IDF dependency
 * (built and tested on the host by test/host/run.sh).
 *
 * The state holds settings, as the Matter Fan Control cluster does: the speed setting (SpeedSetting, 0 = off), the
 * oscillation setting (RockSetting) and the natural wind setting (WindSetting). They are independent of each other:
 * a speed change keeps oscillation and natural wind, natural wind keeps the speed setting. */

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
    bool natural;       /**< natural wind setting; while the fan runs, the fan task varies the speed around the
                             speed setting (fan_natural_next()) */
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
    FAN_CMD_NATURAL_TOGGLE, /**< local natural wind button: on <-> off; turning it on also starts a stopped fan */
    FAN_CMD_SET_NATURAL,    /**< arg = 0 (off) / 1 (on): Matter WindSetting, the speed setting is not changed */
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

/** fan_natural_next() current value when natural wind starts */
#define FAN_NATURAL_START 0xFF

/** Speeds natural wind uses around a speed setting (1..FAN_SPEED_COUNT; other values are clamped):
 *  setting 1: speeds 1-2, setting 2: speeds 1-3, setting 3: speeds 2-3. The motor never stops. */
void fan_natural_range(uint8_t setting, uint8_t *low, uint8_t *high);

/** Natural wind: the next speed of the pattern and how long to hold it. The pattern stays within
 *  fan_natural_range(setting): mostly the lower speeds (the breeze), short gusts at the highest one; with three speeds,
 *  mostly 1 and 2. The speed always changes; it starts (current = FAN_NATURAL_START, or a speed outside the range)
 *  below the gust speed. The hold time is random between min_s and max_s seconds, a gust lasts at most half way.
 *  rng: state of the random generator, any non-zero value to start with. */
uint8_t fan_natural_next(uint32_t *rng, uint8_t current, uint8_t setting, uint16_t min_s, uint16_t max_s,
                         uint32_t *hold_ms);

#ifdef __cplusplus
}
#endif
