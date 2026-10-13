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
 */

/** Create the RMT driver on CONFIG_LED_GPIO and clear the strip. */
esp_err_t led_bar_init(void);

/** Power on/off (Matter OnOff). */
void led_bar_set_power(bool on);

/** Matter CurrentLevel (1..254) -> number of LEDs lit. */
void led_bar_set_level(uint8_t level);

/** Color as full-scale RGB (0..255); the fixed intensity is applied by the driver. */
void led_bar_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/** Number of LEDs that are lit for a given Matter level (exported for logging/tests). */
uint16_t led_bar_count_for_level(uint8_t level);

#ifdef __cplusplus
}
#endif
