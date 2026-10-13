#pragma once

#include <esp_err.h>

/** Install the Device Instance Info provider that reports
 *  - VendorName  = CONFIG_DEVICE_VENDOR_NAME  (Manufacturer in Apple Home)
 *  - ProductName = CONFIG_DEVICE_PRODUCT_NAME (Model in Apple Home)
 *  - SerialNumber = CONFIG_SERIAL_NUMBER_PREFIX + git commit of the build, or the factory MAC address.
 *  Must be called before esp_matter::start(). */
esp_err_t device_info_provider_init();
