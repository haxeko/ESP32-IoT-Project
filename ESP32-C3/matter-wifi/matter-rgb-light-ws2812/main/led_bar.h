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
 *  - the Matter brightness (0-100 %) only chooses HOW MANY of the CONFIG_LED_COUNT LEDs are lit.
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

/** Number of LEDs that are lit for a given Matter level (exported for logging/tests). */
uint16_t led_bar_count_for_level(uint8_t level);

#ifdef __cplusplus
}
#endif
