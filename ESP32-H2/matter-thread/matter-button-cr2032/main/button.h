#pragma once

/*
 * User button on an LP IO (GPIO7..GPIO14 on ESP32-H2; default GPIO10), active low with an external pull-up.
 *
 * No polling: a level interrupt armed for the opposite of the debounced level wakes the button task, which
 * debounces and runs ButtonFsm (button_fsm.h) on task timeouts. The same level is an EXT1 wakeup source, so
 * the chip stays in Light-sleep with the digital peripherals powered down while idle, while the button is
 * held and during the multi-press window: the GPIO interrupt itself cannot wake the chip when the TOP power
 * domain is off, EXT1 (LP IO) can, and the level interrupt then fires once the GPIO registers are restored.
 *
 * Gestures are delivered to the callback on the button task: keep it short and non-blocking (hand Matter
 * work to the CHIP task with PlatformMgr().ScheduleWork()).
 */

#include <stdbool.h>

#include <esp_err.h>

#include "button_fsm.h"

typedef void (*button_event_cb_t)(const ButtonEventInfo &event);

/** Configure the pad as an input (external or internal pull-up, hysteresis). Call it first in app_main(): after
 *  reset the input buffer of GPIO10 is off (ESP32-H2 datasheet, Table 2-1) and gpio_get_level() reads 0, which
 *  for this active-low button means "pressed". Idempotent. */
esp_err_t button_gpio_init(void);

/** button_gpio_init() if not done yet, then the interrupt, the wakeup sources and the button task. */
esp_err_t button_init(button_event_cb_t cb);

/** Raw level now (true = pressed). Valid after button_gpio_init(). */
bool button_is_pressed_raw(void);
