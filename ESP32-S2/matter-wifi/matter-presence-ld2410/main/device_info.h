/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_matter.h>

// Registers the Basic Information provider (vendor and product names shown as
// Manufacturer and Model by controllers). Must be called before esp_matter::start().
void device_info_init(void);

// Sets the default device name (NodeLabel) in the node config.
void device_info_set_name(esp_matter::node::config_t *node_config);

// Adds the ProductLabel and SerialNumber attributes to Basic Information.
void device_info_add_attributes(esp_matter::node_t *node);

// Serial number: the Wi-Fi station MAC address, e.g. "7CDFA1E2F3A4".
void device_info_get_serial(char *buf, size_t size);
