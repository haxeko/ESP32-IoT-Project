#pragma once

#include <esp_err.h>
#include <esp_matter.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

/** Relay state applied at first boot (before anything is stored in NVS) */
#define DEFAULT_POWER false

typedef void *app_driver_handle_t;

/** Configure the relay GPIO (CONFIG_RELAY_GPIO) as output, relay OFF. */
app_driver_handle_t app_driver_relay_init();

/** Initialize the BOOT button (GPIO9): short press toggles the relay. */
app_driver_handle_t app_driver_button_init();

/** Apply an attribute change coming from Matter to the hardware. */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

/** Drive the relay from the OnOff attribute value stored in the data model (restores state after reboot). */
esp_err_t app_driver_relay_set_defaults(uint16_t endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
/* ESP32-H2 runs OpenThread on its own 802.15.4 radio (no RCP, no host) */
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
