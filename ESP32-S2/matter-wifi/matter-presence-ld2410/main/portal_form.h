/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// Form handling of the Wi-Fi setup page. Plain C, also built by the host tests.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decodes application/x-www-form-urlencoded text in place: '+' becomes a
// space and %XX the byte 0xXX; a '%' not followed by two hex digits is kept
// as it is. Returns false when the text encodes a NUL byte (%00), which no
// SSID or password of the Wi-Fi driver can hold.
bool form_url_decode(char *text);

typedef enum {
    WIFI_CREDS_OK,
    WIFI_CREDS_BAD_SSID,        // empty, or longer than 32 bytes
    WIFI_CREDS_BAD_PASSWORD,    // must be empty (open network), 8..63 characters or 64 hex digits
} wifi_creds_check_t;

wifi_creds_check_t wifi_creds_check(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
