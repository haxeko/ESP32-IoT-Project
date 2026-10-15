#pragma once

/* IR remote receiver (RMT) and learning of the remote buttons (menuconfig -> IR Receiver).
 *
 * Learning asks for the remote buttons in this order: POWER, SPEED, SWING (oscillation), OFF, SPEED 1, SPEED 2,
 * SPEED 3, NATURAL (natural wind). The status LED blinks the step number in yellow and flashes green when a button is learned. A click on
 * the device button skips a step (keeps what was learned before for it), a double click ends learning, and so does
 * CONFIG_IR_LEARN_TIMEOUT_S without input. The codes are stored in NVS (namespace "fan"); a factory reset keeps
 * them. */

#include <stdbool.h>

/** Load the learned codes and start receiving. No-op when CONFIG_IR_RX_GPIO is -1. */
void ir_rx_init();

/** Learning control, from any task (the IR task does the work). No-ops without IR receiver. */
void ir_learn_start();
void ir_learn_skip();
void ir_learn_finish();

/** True while learning (buttons then skip / finish instead of controlling the fan). */
bool ir_learn_active();
