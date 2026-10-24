/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// CONFIG mode SoftAP: SSID CONFIG_PRESENCE_AP_SSID_PREFIX + last 4 hex digits of the SoftAP MAC, WPA2-PSK with
// the device password (config_store_ap_password()), page at http://192.168.4.1 (default SoftAP address of
// esp_netif, DHCP server on). Wi-Fi settings are kept in RAM only (no write to the Matter "nvs" partition).
// Never started in NORMAL mode.

#pragma once

#include <stddef.h>

#include <esp_err.h>

#define WIFI_CONFIG_SSID_SIZE 33
#define WIFI_CONFIG_IP_SIZE 16

// Builds the SSID without starting anything (also printed in NORMAL mode)
void wifi_config_ssid(char ssid[WIFI_CONFIG_SSID_SIZE]);

esp_err_t wifi_config_start(const char *password);

// Address of the SoftAP as text ("192.168.4.1"); empty before wifi_config_start()
void wifi_config_ip(char ip[WIFI_CONFIG_IP_SIZE]);

int wifi_config_station_count(void);
