/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Serial number and device name of the board (device_info.cpp), without Matter dependencies.

#pragma once

#include <stddef.h>
#include <stdint.h>

// 12 upper-case hex characters of the 6-byte factory MAC address, e.g. "58CF79A1B2C4"
constexpr size_t kDeviceSerialSize = 13;
void device_format_serial(const uint8_t mac[6], char *buf, size_t size);

// prefix + the last 4 characters of the serial number, e.g. "Presence-B2C4", or display_name when the prefix is
// empty. kDeviceNameSize holds either (prefix at most 11 characters, display name at most 32).
constexpr size_t kDeviceNameSize = 33;
void device_format_name(const char *prefix, const char *display_name, const char *serial, char *buf, size_t size);
