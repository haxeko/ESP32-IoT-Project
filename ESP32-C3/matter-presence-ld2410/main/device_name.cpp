/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include "device_name.h"

void device_format_serial(const uint8_t mac[6], char *buf, size_t size)
{
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void device_format_name(const char *prefix, const char *display_name, const char *serial, char *buf, size_t size)
{
    if (prefix[0] == '\0') {
        snprintf(buf, size, "%s", display_name);
        return;
    }
    // The last 4 characters of the serial number differ from chip to chip
    const size_t len = strlen(serial);
    snprintf(buf, size, "%s%s", prefix, &serial[len > 4 ? len - 4 : 0]);
}
