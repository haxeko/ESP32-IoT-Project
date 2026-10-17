#pragma once

/*
 * Button gestures -> Matter Switch cluster events (Generic Switch, endpoint 1), and the maintenance gestures.
 *
 * Feature map (Kconfig "Matter Switch"):
 *   default            MS | MSR | MSL | MSM, MultiPressMax 2 (CONFIG_SWITCH_MULTI_PRESS_MAX)
 *   SWITCH_ACTION      MS | AS | MSL | MSM: fewer events (no ShortRelease / MultiPressOngoing), Matter 1.4+ only
 *   no multi press     MS | MSR | MSL: no MultiPressComplete wait, lowest single press latency
 * Event sequences follow the certification tests TC-SWTCH-2.3 .. 2.6 (connectedhomeip src/python_testing):
 *   single  InitialPress(1), ShortRelease(1), MultiPressComplete(1, 1)
 *   double  InitialPress(1), ShortRelease(1), InitialPress(1), MultiPressOngoing(1, 2), ShortRelease(1),
 *           MultiPressComplete(1, 2)
 *   long    InitialPress(1), LongPress(1), LongRelease(1)
 *   more presses than MultiPressMax: MultiPressComplete(1, 0)
 * With the AS feature only the first InitialPress and MultiPressComplete are sent for a multi-press sequence.
 *
 * Not commissioned: a short press (re)opens the BLE commissioning window instead.
 * Hold 10 s then release: open a commissioning window (Thread/DNS-SD if commissioned, for a second controller).
 * Hold 20 s then release: factory reset. Held > 40 s: cancelled (stuck button).
 */

#include <esp_err.h>
#include <esp_matter.h>

#include "button_fsm.h"

/** Create the Generic Switch endpoint (Identify + Switch). Before esp_matter::start(). Returns its id or 0. */
uint16_t switch_events_create_endpoint(esp_matter::node_t *node);

/** Button task callback (button_init()). */
void switch_events_on_button(const ButtonEventInfo &event);

/** Identify cluster callback hooks (CHIP task). */
void switch_events_identify(bool on);

/** Factory reset after the LED confirmation; any task. */
void switch_events_factory_reset(void);
