#pragma once

/* Push buttons (menuconfig -> Buttons), all active low with the internal pull-up.
 *
 * Main button (BOOT, GPIO9):
 *   click         fan on/off                    (while learning the remote: skip this button)
 *   double click  pairing: open the commissioning window for 15 min (while learning: finish learning)
 *   triple click  learn the IR remote
 *   hold 5 s      LED turns red; release: factory reset (Wi-Fi and Matter pairings are erased)
 * Optional speed button: next speed. Optional oscillation button: oscillation on/off. */

/** Callbacks of the actions that need Matter */
typedef struct {
    void (*open_pairing)(void);  /**< double click, from the esp_timer task: must not block */
    void (*factory_reset)(void); /**< hold 5 s + release, from a dedicated task after the LED feedback */
} buttons_actions_t;

void buttons_init(const buttons_actions_t *actions);
