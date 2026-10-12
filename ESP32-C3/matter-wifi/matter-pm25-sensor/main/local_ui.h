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

// Air quality level shown on the OLED and LEDs, from PM2.5 in µg/m³ (US EPA
// 2024 AQI): 0 RẤT TỐT, 1 TỐT, 2 TRUNG BÌNH, 3 KÉM, 4 XẤU.
int local_ui_pm25_level(float pm25);

// New PM2.5 value in µg/m³. Can be called from any task.
void local_ui_set_pm25(float pm25);

// Shows a small icon in the corner of the OLED while the device is
// commissioned and online. Can be called from any task.
void local_ui_set_matter_connected(bool connected);

// Test mode: replaces the air quality label with this text (NULL to clear)
// and keeps the OLED on. The string must stay valid.
void local_ui_set_test_label(const char *label);
