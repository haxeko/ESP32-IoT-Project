/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Temperature of the ESP32-C3 (its internal sensor) as a Matter Temperature Sensor endpoint (device type 0x0302):
// Home Assistant shows it as a temperature entity of the device. It is the temperature of the chip, above the room
// temperature (the Wi-Fi radio, the radar and the regulator warm the board): it shows whether the board gets hot, not
// how warm the room is. The LD2410 has no command that reads its temperature.

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Before esp_matter::start(), after the radar controls (endpoint 5). Starts the sensor; no endpoint when it cannot.
esp_err_t c3_temperature_create();

// Matter task, once the server is up: the first value at once, then a reading every minute.
void c3_temperature_start();

// MeasuredValue unit: 0.01 °C. The ESP32-C3 works from -40 to 125 °C.
constexpr int16_t kC3TemperatureMin = -4000;
constexpr int16_t kC3TemperatureMax = 12500;

// °C to MeasuredValue, rounded and kept within the range above (a NaN gives the minimum).
inline int16_t c3_temperature_to_measured(float celsius)
{
    const float value = celsius * 100.0f;
    if (!(value > kC3TemperatureMin)) {
        return kC3TemperatureMin;
    }
    if (value >= kC3TemperatureMax) {
        return kC3TemperatureMax;
    }
    return (int16_t)(value < 0 ? value - 0.5f : value + 0.5f);
}

// Reported again when it changed by step (MeasuredValue unit) or more since the last report, or nothing reported yet.
inline bool c3_temperature_due(bool reported, int16_t last, int16_t now, int16_t step)
{
    return !reported || (now > last ? now - last : last - now) >= step;
}
