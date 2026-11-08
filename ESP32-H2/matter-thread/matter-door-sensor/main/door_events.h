#pragma once

/*
 * Door contact -> Matter Contact Sensor (device type 0x0015) on endpoint 1: Identify + Boolean State. StateValue
 * true = contact = door closed (magnet at the reed), as Home Assistant and Apple Home read it; a StateChange event on
 * each change.
 *
 * No button gestures. Commissioning and reset without a button (README, "Ghép nối không cần nút"):
 *  - not commissioned: the commissioning window opens at boot (BLE + DNS-SD, 15 min); afterwards any door movement
 *    opens a new one, and wakes the chip from the Deep-sleep it enters once a window closed unused (power.cpp)
 *  - another controller (Multi-Admin): the current controller's "pairing mode / share" (Enhanced Commissioning), or
 *    the magnet gesture (CONFIG_CONTACT_GESTURE_CHANGES changes within CONFIG_CONTACT_GESTURE_WINDOW_MS) for a basic
 *    window with the original code, over Thread/DNS-SD
 *  - factory reset: remove the device from its last controller (app_main.cpp), or insert the battery
 *    CONFIG_POWER_CYCLE_RESET_COUNT times in a row (power_cycle.h)
 */

#include <esp_err.h>
#include <esp_matter.h>

#include "contact_fsm.h"

/** Create the Contact Sensor endpoint (Identify + Boolean State). Before esp_matter::start(). Returns its id or 0. */
uint16_t door_events_create_endpoint(esp_matter::node_t *node, bool closed);

/** Contact task callback (contact_init()). */
void door_events_on_contact(const ContactEventInfo &event, bool initial);

/** Identify cluster callback hooks (CHIP task). */
void door_events_identify(bool on);

/** Factory reset after the LED confirmation; CHIP task. */
void door_events_factory_reset(void);
