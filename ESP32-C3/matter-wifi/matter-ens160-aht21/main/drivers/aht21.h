/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// AHT21 temperature and humidity sensor (Aosong; the AHT20 and AHT25 answer the same way), I2C address 0x38. On the
// common "ENS160 + AHT21" module it is the part that measures the temperature: the ENS160 only measures gases.
//
// Blocking calls, for one task only (the sensor task). The first call must come at least 100 ms after power-on.

#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <stdint.h>

constexpr uint16_t kAht21Address = 0x38;

// Adds the sensor to the bus (once), soft-resets it and initializes its calibration registers when the status asks
// for it (datasheet, "sensor reading process"). ESP_ERR_NOT_FOUND or an I2C error: no sensor answers.
esp_err_t aht21_init(i2c_master_bus_handle_t bus);

// Starts a measurement and waits for it (80 ms, up to 130 ms). ESP_ERR_INVALID_CRC: corrupted frame;
// ESP_ERR_INVALID_STATE: the sensor lost its initialization, call aht21_init() again; ESP_ERR_TIMEOUT: still busy.
esp_err_t aht21_read(float *temperature_c, float *humidity_pct);
