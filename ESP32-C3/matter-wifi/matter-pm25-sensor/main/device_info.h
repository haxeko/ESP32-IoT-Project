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

// Sets the default device name (NodeLabel) in the node config and adds the
// ProductLabel attribute, so controllers propose that name when pairing.
void device_info_set_name(esp_matter::node::config_t *node_config);
void device_info_add_product_label(esp_matter::node_t *node);
