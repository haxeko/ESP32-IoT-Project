#pragma once

#include <esp_err.h>

/** Install the Device Instance Info provider whose SerialNumber is
 *  CONFIG_SERIAL_NUMBER_PREFIX + the factory MAC address (12 uppercase hex digits).
 *  Must be called before esp_matter::start(). */
esp_err_t serial_number_provider_init();
