#pragma once

/* Fan task: owns the fan state (fan_logic.h), drives the relays and stores the state in NVS.
 * Matter, the IR remote and the buttons only post commands to it, so they never race each other. */

#include <stdbool.h>
#include <stdint.h>

#include <fan_logic.h>

/** Where a command comes from */
typedef enum {
    FAN_SRC_MATTER = 0,
    FAN_SRC_IR,
    FAN_SRC_BUTTON,
    FAN_SRC_NATURAL, /**< the fan task itself: natural wind changed the speed (listener only, not for posts) */
} fan_src_t;

/** Called by the fan task after every command, also when the state did not change (a Matter write of 45 %
 *  leaves a fan at speed 1 unchanged, but the controller still has to see 33 %). */
typedef void (*fan_listener_t)(const fan_state_t *state, fan_src_t src, bool changed);

/** Switch every relay off, restore the stored state (menuconfig -> Fan Behavior) and start the fan task, which
 *  applies it. Call early in app_main, after nvs_flash_init(). */
void fan_ctrl_init();

/** Queue a command for the fan task. Non-blocking, from any task (not from an ISR).
 *  Returns false when the queue is full or the fan task is not running. */
bool fan_ctrl_post(const fan_cmd_t *cmd, fan_src_t src);
bool fan_ctrl_post_type(fan_cmd_type_t type, uint8_t arg, fan_src_t src);

/** Snapshot of the current state, from any task. */
fan_state_t fan_ctrl_get_state();

/** Speed the motor runs at, from any task: state.speed, or the speed of the moment while natural wind runs.
 *  The listener is also called (src FAN_SRC_NATURAL) when natural wind changes it. */
uint8_t fan_ctrl_get_drive_speed();

/** Register the (single) listener. Call before esp_matter::start(). */
void fan_ctrl_set_listener(fan_listener_t listener);
