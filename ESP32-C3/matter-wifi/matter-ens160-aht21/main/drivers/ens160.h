/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ENS160 digital metal-oxide gas sensor (ScioSense): air quality index AQI-UBA, TVOC and eCO2 (CO2 estimated from
// the VOCs). It measures no temperature: it expects the ambient temperature and humidity from the host
// (TEMP_IN / RH_IN) to compensate its readings, which the AHT21 of the module provides.
//
// Blocking calls, for one task only (the sensor task).

#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <stdint.h>

// ADD pin high (most modules, including the ENS160 + AHT21 one) or low
constexpr uint16_t kEns160AddressHigh = 0x53;
constexpr uint16_t kEns160AddressLow = 0x52;
constexpr uint16_t kEns160PartId = 0x0160;
constexpr uint16_t kEns161PartId = 0x0161; // its successor, with the same registers

typedef struct {
    uint8_t status;    // DEVICE_STATUS: flags and validity (sensor_math.h)
    bool new_data;     // the sensor had new values, read into the fields below
    uint8_t aqi;       // AQI-UBA 1 (excellent) to 5 (unhealthy)
    uint16_t tvoc_ppb; // 0 to 65000 ppb
    uint16_t eco2_ppm; // 400 to 65000 ppm
} ens160_data_t;

// Finds the sensor at 0x53 or 0x52, checks its part ID (ENS160 or ENS161) and starts the standard (gas sensing)
// mode. A sensor that still runs in standard mode (the ESP32 restarted, the sensor kept its power) is left running,
// so that it does not warm up again. reset: reset the sensor first (after errors). ESP_ERR_NOT_FOUND: no sensor
// answers.
esp_err_t ens160_init(i2c_master_bus_handle_t bus, bool reset);

// Address found by ens160_init(), 0 before.
uint16_t ens160_address(void);

// Ambient temperature and humidity used by the sensor to compensate its readings (25 °C and 50 % until set).
esp_err_t ens160_set_compensation(float temperature_c, float humidity_pct);

// Reads the status; when the sensor has new values (new_data), also reads them, otherwise leaves aqi, tvoc_ppb and
// eco2_ppm as they were. The sensor measures once a second in standard mode.
esp_err_t ens160_read(ens160_data_t *data);
