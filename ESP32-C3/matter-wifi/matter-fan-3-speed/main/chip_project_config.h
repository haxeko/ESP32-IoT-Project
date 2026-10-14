// Matter SDK overrides, included by CHIPConfig.h through CONFIG_CHIP_PROJECT_CONFIG.

#pragma once

#include "sdkconfig.h"

// Advertise the device name in commissionable node discovery (DNS-SD "DN" key), so controllers can show
// "Smart Fan" before pairing instead of a generic "Matter Accessory". At most 32 characters.
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1
#define CHIP_DEVICE_CONFIG_DEVICE_NAME CONFIG_DEVICE_DISPLAY_NAME
