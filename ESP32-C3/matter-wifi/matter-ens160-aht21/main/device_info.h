/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Basic Information shown by controllers: Manufacturer, Model, Hardware, Serial number, and the device name.

#pragma once

#include <esp_matter.h>

#include "device_name.h"

// Registers the Basic Information provider: VendorName, ProductName, ProductLabel (= ProductName, the model in
// Home Assistant), HardwareVersionString and SerialNumber from menuconfig and the eFuse MAC. Logs the serial
// number and device name. Before esp_matter::start().
void device_info_init(void);

// Adds the ProductLabel and SerialNumber attributes to Basic Information. Before esp_matter::start().
void device_info_add_attributes(esp_matter::node_t *node);

// After esp_matter::start(): writes the device name to NodeLabel when no label is stored yet (new board, after a
// factory reset), and makes it the Bluetooth (GAP) name used while waiting to be commissioned.
void device_info_apply_name(void);

// Serial number: the factory base MAC address in eFuse, also the Wi-Fi station MAC, e.g. "58CF79A1B2C4".
void device_info_get_serial(char *buf, size_t size);

// CONFIG_DEVICE_NAME_PREFIX + the last 4 characters of the serial number, e.g. "ENS160-B2C4", or
// CONFIG_DEVICE_DISPLAY_NAME when the prefix is empty.
void device_info_get_name(char *buf, size_t size);
