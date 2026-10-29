// Optional single-color status LED on a GPIO (CONFIG_STATUS_LED_ENABLE, off by default).
// Without it every function does nothing, so callers need no #if. Safe to call from any task.
//
// One pattern at a time, highest priority first:
//   factory reset running      solid on
//   reset button held          fast blink (100 ms)
//   Identify (from controller) medium blink (200 ms)
//   commissioning window open  slow blink (500 ms)
//   door open                  on, only with CONFIG_STATUS_LED_SHOW_CONTACT
//   otherwise                  off

#pragma once

void status_led_init(void);
void status_led_set_resetting(void);
void status_led_set_reset_hold(bool held);
void status_led_set_identify(bool on);
void status_led_set_commissioning(bool window_open);
void status_led_set_contact_open(bool open);
