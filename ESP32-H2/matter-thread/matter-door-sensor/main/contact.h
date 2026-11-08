#pragma once

/*
 * Door contact input: a reed switch from the button net (BTN_SW) to GND, in parallel with SW1 or in its place, on an
 * LP IO (GPIO7..GPIO14 on ESP32-H2; default GPIO10) with the board's external 1 MOhm pull-up (R6).
 *
 * As the button driver it replaces: no polling. A level interrupt armed for the opposite of the debounced level wakes
 * the contact task, which debounces and runs ContactFsm (contact_fsm.h) on task timeouts. The same level is an EXT1
 * wakeup source, so the chip stays in Light-sleep with the digital peripherals powered down whether the door stays
 * open or closed for hours: the GPIO interrupt cannot wake the chip when the TOP power domain is off, EXT1 (LP IO)
 * can, and the level interrupt then fires once the GPIO registers are restored.
 *
 * Events go to the callback on the contact task: keep it short and non-blocking (hand Matter work to the CHIP task
 * with PlatformMgr().ScheduleWork()). The first call, right after contact_init(), reports the state at start.
 */

#include <stdbool.h>

#include <esp_err.h>

#include "contact_fsm.h"

typedef void (*contact_event_cb_t)(const ContactEventInfo &event, bool initial);

/** Configure the pad as an input (external or internal pull-up, hysteresis). Call it first in app_main(): after
 *  reset the input buffer of GPIO10 is off (ESP32-H2 datasheet, Table 2-1) and gpio_get_level() reads 0. Idempotent. */
esp_err_t contact_gpio_init(void);

/** contact_gpio_init() if not done yet, then the interrupt, the wakeup sources and the contact task. */
esp_err_t contact_init(contact_event_cb_t cb);

/** Raw state now (true = closed, door shut). Valid after contact_gpio_init(). */
bool contact_is_closed_raw(void);

/** Before esp_deep_sleep_start(): wake on the level opposite to the present one, so any door movement starts the
 *  chip again (external pull-up builds; with the internal pull-up the pad is not defined in Deep-sleep). */
void contact_enable_deep_sleep_wakeup(void);
