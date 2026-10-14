#pragma once

#include <stddef.h>

#include <esp_matter.h>

/** Install the Device Instance Info provider (Basic Information):
 *  - VendorName            = CONFIG_DEVICE_VENDOR_NAME             (Manufacturer in Apple Home)
 *  - ProductName           = CONFIG_DEVICE_PRODUCT_NAME            (Model in Apple Home)
 *  - ProductLabel          = CONFIG_DEVICE_DISPLAY_NAME
 *  - HardwareVersionString = CONFIG_DEVICE_HARDWARE_VERSION_STRING (Hardware in Home Assistant)
 *  - SerialNumber          = Wi-Fi MAC address, 12 uppercase hex digits
 *  Must be called before esp_matter::start(). */
void device_info_init(void);

/** Default device name (NodeLabel) in the node config. */
void device_info_set_name(esp_matter::node::config_t *node_config);

/** Add the optional ProductLabel and SerialNumber attributes to Basic Information (before esp_matter::start()). */
void device_info_add_attributes(esp_matter::node_t *node);
