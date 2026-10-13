#pragma once

/** Install the Device Instance Info provider that reports
 *  - VendorName            = CONFIG_DEVICE_VENDOR_NAME             (Manufacturer in Apple Home)
 *  - ProductName           = CONFIG_DEVICE_PRODUCT_NAME            (Model in Apple Home)
 *  - HardwareVersionString = CONFIG_DEVICE_HARDWARE_VERSION_STRING (Hardware in Home Assistant)
 *  - SerialNumber          = CONFIG_SERIAL_NUMBER_PREFIX + factory MAC address (12 uppercase hex digits)
 *  Must be called before esp_matter::start(). The SerialNumber attribute itself must also be created
 *  on the root endpoint (it is optional in Basic Information), see app_main(). */
void device_info_provider_init();
