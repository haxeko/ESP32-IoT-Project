#include "fan_logic.h"

uint8_t fan_percent_to_speed(uint8_t percent)
{
    if (percent == 0) {
        return 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    /* Round to the nearest speed; any non-zero percent runs the fan */
    unsigned speed = ((unsigned)percent * FAN_SPEED_COUNT + 50) / 100;
    return speed < 1 ? 1 : (uint8_t)speed;
}

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
        break;
    case FAN_CMD_SET_SPEED:
        speed = cmd->arg > FAN_SPEED_COUNT ? FAN_SPEED_COUNT : cmd->arg;
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
        break;
    case FAN_CMD_STEP_DOWN:
        if (state->speed > lowest) {
            speed = state->speed - 1;
        } else if (cmd->wrap) {
            speed = FAN_SPEED_COUNT;
        }
        break;
    default:
        return false;
    }

    state->speed = speed;
    if (speed) {
        state->last_speed = speed;
    }
    return state->speed != old.speed || state->last_speed != old.last_speed || state->swing != old.swing;
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
