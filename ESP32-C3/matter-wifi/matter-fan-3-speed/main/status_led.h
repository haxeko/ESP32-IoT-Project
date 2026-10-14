#pragma once

/* Status LED (menuconfig -> Status LED): single-color GPIO LED or one WS2812, or none.
 * All functions can be called from any task; a small LED task renders the highest-priority indication:
 *   factory reset (red) > short flash (green) > IR learning (yellow, N blinks) > Identify (blue)
 *   > pairing (blue, slow blink) > fan running (dim green, optional) > off.
 * A single-color LED shows the same patterns without the colors. */

#include <stdbool.h>
#include <stdint.h>

/** Start the LED task. Safe to call when the LED is disabled (everything becomes a no-op). */
void status_led_init();

/** False when there is no LED (STATUS_LED_TYPE_NONE): Matter Identify then reports no indicator. */
bool status_led_present();

/** Pairing: slow blue blink while the device can be commissioned. */
void status_led_pairing(bool pairing);

/** Matter Identify: blue blinks (0.5 s on, 0.5 s off). blinks = 0 blinks until status_led_identify_stop(). */
void status_led_identify(uint16_t blinks);
void status_led_identify_stop();
/** Identify FinishEffect: complete the blink in progress, then stop. */
void status_led_identify_finish();

/** IR learning: step 1..N blinks yellow N times, then a pause; 0 ends the indication. */
void status_led_learning(uint8_t step);

/** Short green flash: an IR command was accepted or a remote button was learned. */
void status_led_flash();

/** Factory reset: solid red while the button is held long enough (release to reset). */
void status_led_reset_armed(bool armed);
/** Factory reset in progress: red blinking until the device restarts. */
void status_led_reset_running();

/** Fan running (only shown with CONFIG_STATUS_LED_SHOW_RUNNING). */
void status_led_running(bool running);
