/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_matter.h>

// Registers the Basic Information provider (vendor and product names shown as
// Manufacturer and Model by controllers, ProductLabel = ProductName). Must be
// called before esp_matter::start().
void device_info_init(void);

// Device name: CONFIG_DEVICE_NAME_PREFIX + the last 4 characters of the
// serial number, e.g. "Presence-B2C3", or CONFIG_DEVICE_DISPLAY_NAME when the
// prefix is empty. kDeviceNameSize holds either.
constexpr size_t kDeviceNameSize = 33;
void device_info_get_name(char *buf, size_t size);

// After esp_matter::start(): writes the device name to NodeLabel when no label
// is stored yet (new board, after a factory reset), and makes it the Bluetooth
// (GAP) name used while waiting to be commissioned.
void device_info_apply_name(void);

// Adds the ProductLabel and SerialNumber attributes to Basic Information.
void device_info_add_attributes(esp_matter::node_t *node);

// Serial number: the factory base MAC address in eFuse, e.g. "744DBDA1B2C3".
void device_info_get_serial(char *buf, size_t size);
