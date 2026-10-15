#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WS2812 strip used as a "bar" light:
 *  - every lit LED always shines at the same fixed intensity (CONFIG_LED_FIXED_BRIGHTNESS_PERCENT, 30 %),
 *  - the Matter brightness (0-100 %) only chooses HOW MANY of the CONFIG_LED_COUNT LEDs are lit: directly, or in
 *    lift mode through the height of the lamp (the LEDs the moving part has exposed).
 *
 * All functions are thread-safe. The strip is only refreshed when the visible output changes.
 */

/** Create the RMT driver on CONFIG_LED_GPIO and clear the strip. */
esp_err_t led_bar_init(void);

/** Set power, Matter CurrentLevel (1..254) and full-scale RGB color at once (one refresh). */
void led_bar_set_state(bool on, uint8_t level, uint8_t r, uint8_t g, uint8_t b);

/** Power on/off (Matter OnOff). */
void led_bar_set_power(bool on);

/** Current power state (the OnOff value last applied). */
bool led_bar_get_power(void);

/** Matter CurrentLevel (1..254) -> number of LEDs lit. */
void led_bar_set_level(uint8_t level);

/** Color as full-scale RGB (0..255); the fixed intensity is applied by the driver. */
void led_bar_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * Identify: blink the whole strip in the current color, 0.5 s on / 0.5 s off.
 * @param blinks number of on/off cycles, or 0 to blink until led_bar_identify_stop().
 */
void led_bar_identify_start(uint16_t blinks);

/** Stop blinking and show the normal state again. */
void led_bar_identify_stop(void);

/** Number of LEDs that are lit for a given Matter level (also the number exposed at the height for that level). */
uint16_t led_bar_count_for_level(uint8_t level);

/**
 * Lift mode: from now on, while on, exactly `count` LEDs are lit (the part of the strip the moving upper part has
 * exposed), whatever the level. The lift calls it as the lamp moves. Identify blinks that part (at least one LED).
 */
void led_bar_set_exposed(uint16_t count);

/** Back to the default: the level chooses how many LEDs are lit (no lift, or the motor failed). */
void led_bar_follow_level(void);

/**
 * Called with the height the lamp should have: on/off and the level being shown (the level kept during the
 * Matter "On" fade-in, see led_bar.c). Called only on changes, and once at registration, with the driver's lock
 * held: it must not call led_bar functions or block.
 */
typedef void (*led_bar_target_cb_t)(bool on, uint8_t level);
void led_bar_set_target_callback(led_bar_target_cb_t cb);

#ifdef __cplusplus
}
#endif
