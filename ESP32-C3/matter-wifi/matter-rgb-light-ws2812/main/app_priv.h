#pragma once

#include <esp_err.h>
#include <esp_matter.h>

/** State applied at first boot (before anything is stored in NVS): light on, 100 % (all LEDs), white */
#define DEFAULT_POWER      true
#define DEFAULT_BRIGHTNESS 254

typedef void *app_driver_handle_t;

/** Initialize the WS2812 strip (CONFIG_LED_GPIO, CONFIG_LED_COUNT). */
app_driver_handle_t app_driver_light_init();

/** Initialize the button (CONFIG_APP_BUTTON_GPIO): short press toggles, hold 5 s = factory reset. */
void app_driver_button_init();

/** Apply an attribute change coming from Matter to the LEDs. */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

/** Drive the LEDs from the attribute values stored in the data model (restores state after reboot). */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);
