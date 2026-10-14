#pragma once

#include <esp_err.h>
#include <esp_matter.h>

/** State applied at first boot (before anything is stored in NVS): light on, 100 % (all LEDs), white */
#define DEFAULT_POWER      true
#define DEFAULT_BRIGHTNESS 254

/** Endpoint of the light (set in app_main() before the Matter stack starts, read-only afterwards). */
extern uint16_t light_endpoint_id;

/** Initialize the WS2812 strip (CONFIG_LED_GPIO, CONFIG_LED_COUNT). */
esp_err_t app_driver_light_init();

/**
 * Initialize the button (CONFIG_APP_BUTTON_GPIO): a short press toggles the light.
 * @return the button handle (for app_reset_button_register), or NULL if there is no button.
 */
void *app_driver_button_init();

/**
 * Toggle the light as a local control (button). Can be called from any task: the change runs later on the
 * Matter thread, which updates the OnOff attribute, the LEDs and reports it to controllers. Does not block.
 */
void app_driver_light_toggle();

/** Apply an attribute change coming from Matter to the LEDs. Called on the Matter thread. */
esp_err_t app_driver_attribute_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val);

/** Identify cluster: blink the light. Called on the Matter thread. */
esp_err_t app_driver_identify(esp_matter::identification::callback_type_t type, uint8_t effect_id);

/** Drive the LEDs from the attribute values stored in the data model (restores state after reboot). */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);
