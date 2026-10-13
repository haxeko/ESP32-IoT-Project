/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdbool.h>

// ESP32-S2 has no Bluetooth, so a phone cannot hand over the Wi-Fi credentials
// during Matter commissioning the usual way. Instead the device first joins the
// home Wi-Fi through a small setup web page, then it is commissioned
// "on-network" (the controller finds it over Wi-Fi with DNS-SD).

// True when Wi-Fi station credentials are stored (saved by the setup page, or
// CONFIG_DEFAULT_WIFI_SSID is set). Call after nvs_flash_init() and before
// esp_matter::start().
bool wifi_setup_is_provisioned(void);

// Applies CONFIG_WIFI_MAX_TX_POWER. Call after the Wi-Fi driver is started.
void wifi_setup_limit_tx_power(void);

// Starts the access point "<CONFIG_WIFI_SETUP_AP_PREFIX><last 4 MAC digits>"
// with a captive setup page at http://192.168.4.1. The chosen network is tested
// first; once the device got an IP address, the credentials are saved and the
// chip restarts into Matter. Never returns.
void wifi_setup_run_portal(void);
