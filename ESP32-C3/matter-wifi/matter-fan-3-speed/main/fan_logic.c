#include "fan_logic.h"

uint8_t fan_speed_to_percent(uint8_t speed)
{
    if (speed > FAN_SPEED_COUNT) {
        speed = FAN_SPEED_COUNT;
    }
    return (uint8_t)((unsigned)speed * 100 / FAN_SPEED_COUNT);
}

static uint8_t resume_speed(const fan_state_t *state)
{
    uint8_t s = state->last_speed;
    return (s >= 1 && s <= FAN_SPEED_COUNT) ? s : FAN_DEFAULT_SPEED;
}

bool fan_state_apply(fan_state_t *state, const fan_cmd_t *cmd)
{
    const fan_state_t old = *state;
    const uint8_t lowest = cmd->lowest_off ? 0 : 1;
    uint8_t speed = state->speed;
    bool manual = false; /* the command selects a speed: natural wind ends when the fan runs */

    switch ((fan_cmd_type_t)cmd->type) {
    case FAN_CMD_TOGGLE:
        speed = state->speed ? 0 : resume_speed(state);
        break;
    case FAN_CMD_ON:
        if (state->speed == 0) {
            speed = resume_speed(state);
        }
        break;
    case FAN_CMD_OFF:
        speed = 0;
        break;
    case FAN_CMD_SPEED_CYCLE:
        speed = state->speed ? (uint8_t)(state->speed % FAN_SPEED_COUNT + 1) : resume_speed(state);
        manual = true;
        break;
    case FAN_CMD_SET_SPEED:
        speed = cmd->arg > FAN_SPEED_COUNT ? FAN_SPEED_COUNT : cmd->arg;
        manual = true;
        break;
    case FAN_CMD_SWING_TOGGLE:
        state->swing = !state->swing;
        break;
    case FAN_CMD_SET_SWING:
        state->swing = cmd->arg != 0;
        break;
    case FAN_CMD_STEP_UP:
        if (state->speed < FAN_SPEED_COUNT) {
            speed = state->speed + 1;
        } else if (cmd->wrap) {
            speed = lowest;
        }
        manual = true;
        break;
    case FAN_CMD_STEP_DOWN:
        if (state->speed > lowest) {
            speed = state->speed - 1;
        } else if (cmd->wrap) {
            speed = FAN_SPEED_COUNT;
        }
        manual = true;
        break;
    case FAN_CMD_NATURAL_TOGGLE:
        if (state->speed && state->natural) {
            state->natural = false;
        } else {
            state->natural = true;
            if (state->speed == 0) {
                speed = resume_speed(state);
            }
        }
        break;
    case FAN_CMD_SET_NATURAL:
        state->natural = cmd->arg != 0;
        if (state->natural && state->speed == 0) {
            speed = resume_speed(state);
        }
        break;
    default:
        return false;
    }

    state->speed = speed;
    if (speed) {
        state->last_speed = speed;
        if (manual) {
            state->natural = false;
        }
    }
    return state->speed != old.speed || state->last_speed != old.last_speed || state->swing != old.swing ||
           state->natural != old.natural;
}

void fan_state_sanitize(fan_state_t *state)
{
    if (state->speed > FAN_SPEED_COUNT) {
        state->speed = 0;
    }
    if (state->speed) {
        /* Same invariant as fan_state_apply(): a running fan resumes at its own speed */
        state->last_speed = state->speed;
    } else if (state->last_speed < 1 || state->last_speed > FAN_SPEED_COUNT) {
        state->last_speed = FAN_DEFAULT_SPEED;
    }
}

bool fan_state_swing_running(const fan_state_t *state)
{
    return state->swing && state->speed > 0;
}

bool fan_state_natural_running(const fan_state_t *state)
{
    return state->natural && state->speed > 0;
}

/* xorshift32 */
static uint32_t next_random(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : 0x9E3779B9UL;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

uint8_t fan_natural_next(uint32_t *rng, uint8_t current, uint16_t min_s, uint16_t max_s, uint32_t *hold_ms)
{
    _Static_assert(FAN_SPEED_COUNT == 3, "the natural wind pattern is written for 3 speeds");
    const uint32_t pct = next_random(rng) % 100;
    uint8_t next;
    switch (current) {
    case 1:
        next = pct < 75 ? 2 : 3; /* breeze, sometimes a gust */
        break;
    case 2:
        next = pct < 55 ? 1 : 3;
        break;
    case 3:
        next = pct < 70 ? 2 : 1;
        break;
    default:
        next = pct < 50 ? 1 : 2; /* natural wind starts low */
        break;
    }

    if (min_s < 1) {
        min_s = 1;
    }
    if (max_s < min_s) {
        max_s = min_s;
    }
    const uint32_t lo = (uint32_t)min_s * 1000;
    uint32_t hi = (uint32_t)max_s * 1000;
    if (next == FAN_SPEED_COUNT) {
        hi = lo + (hi - lo) / 2; /* gust */
    }
    *hold_ms = lo + next_random(rng) % (hi - lo + 1);
    return next;
}
