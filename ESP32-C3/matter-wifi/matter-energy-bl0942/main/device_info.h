#pragma once

#include <esp_err.h>
#include <esp_matter.h>

/**
 * Device identity shown by Matter controllers (Basic Information cluster):
 *   - VendorName   = CONFIG_DEVICE_INFO_VENDOR_NAME   ("Manufacturer" in Apple Home)
 *   - ProductName  = CONFIG_DEVICE_INFO_PRODUCT_NAME  ("Model", default accessory name when pairing)
 *   - ProductLabel = CONFIG_DEVICE_INFO_PRODUCT_NAME
 *   - SerialNumber = Wi-Fi MAC address, 12 hex digits
 *
 * Call after node::create() and before esp_matter::start().
 */
esp_err_t device_info_init(esp_matter::node_t *node);
