/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "portal_form.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool form_url_decode(char *text)
{
    char *out = text;
    for (const char *p = text; *p; p++) {
        if (*p == '+') {
            *out++ = ' ';
            continue;
        }
        if (*p == '%') {
            const int hi = hex_value(p[1]);
            const int lo = hi < 0 ? -1 : hex_value(p[2]);
            if (lo >= 0) {
                const char c = (char)(hi * 16 + lo);
                if (c == 0) {
                    *out = 0;
                    return false;
                }
                *out++ = c;
                p += 2;
                continue;
            }
        }
        *out++ = *p;
    }
    *out = 0;
    return true;
}

wifi_creds_check_t wifi_creds_check(const char *ssid, const char *password)
{
    const size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > 32) {
        return WIFI_CREDS_BAD_SSID;
    }
    const size_t pass_len = strlen(password);
    if (pass_len == 0 || (pass_len >= 8 && pass_len <= 63)) {
        return WIFI_CREDS_OK;
    }
    if (pass_len == 64) {
        // A 64-character key is the raw PSK in hex.
        for (size_t i = 0; i < pass_len; i++) {
            if (hex_value(password[i]) < 0) {
                return WIFI_CREDS_BAD_PASSWORD;
            }
        }
        return WIFI_CREDS_OK;
    }
    return WIFI_CREDS_BAD_PASSWORD;
}
