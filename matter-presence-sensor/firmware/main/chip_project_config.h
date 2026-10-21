/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter SDK overrides, included by CHIPConfig.h through CONFIG_CHIP_PROJECT_CONFIG.

#pragma once

#include "sdkconfig.h"

// Advertise the device name in commissionable node discovery (DNS-SD "DN" key), so controllers can show
// "Presence Sensor" before pairing instead of a generic "Matter Accessory". At most 32 characters.
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1
#define CHIP_DEVICE_CONFIG_DEVICE_NAME CONFIG_DEVICE_DISPLAY_NAME
