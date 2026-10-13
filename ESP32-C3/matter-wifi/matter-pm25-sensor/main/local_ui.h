/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>

// On-device UI that runs without any controller: OLED, WS2812 air quality
// indicator and the LD2410 presence radar that wakes the OLED.
esp_err_t local_ui_init(void);

// Air quality level shown on the OLED and LEDs, from PM2.5 in µg/m³:
// 0 TỐT (<= 12), 1 KHÁ (<= 35.4), 2 KHÔNG TỐT (<= 55.4), 3 KÉM (<= 125.4),
// 4 CỰC KỲ KÉM. Returns -1 when there is no value (NAN or negative).
int local_ui_pm25_level(float pm25);

// New PM2.5 value in µg/m³. Can be called from any task. A missing value
// (NAN) is ignored: the last value stays on the OLED and LEDs.
void local_ui_set_pm25(float pm25);

// Shows a small icon at the top center of the OLED while the device is online
// and at least one controller is subscribed. Can be called from any task.
void local_ui_set_matter_connected(bool connected);

// Factory reset feedback. Unless it is LOCAL_UI_RESET_NONE, the OLED turns on
// and shows it instead of the measurement. Can be called from any task.
typedef enum {
    LOCAL_UI_RESET_NONE,        // normal screen
    LOCAL_UI_RESET_HOLD,        // button held: seconds_left until releasing it resets
    LOCAL_UI_RESET_RELEASE,     // held long enough: releasing the button resets
    LOCAL_UI_RESET_RUNNING,     // factory reset in progress, the device restarts
} local_ui_reset_t;

void local_ui_set_reset(local_ui_reset_t reset, int seconds_left);
