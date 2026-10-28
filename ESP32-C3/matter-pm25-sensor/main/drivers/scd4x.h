/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;         // false when the sensor stopped sending measurements
    float co2_ppm;
} scd4x_data_t;

typedef void (*scd4x_cb_t)(const scd4x_data_t *data, void *user_data);

typedef struct {
    i2c_master_bus_handle_t bus;    // shared I2C bus (i2c_bus.h)
    uint32_t report_interval_ms;    // the measurements (one every 5 s) are averaged and reported at this interval
    scd4x_cb_t cb;                  // called from the sensor task
    void *user_data;
} scd4x_config_t;

// Sensirion SCD40 / SCD41 CO2 sensor at I2C address 0x62. A sensor that does
// not answer (yet) is retried in the background.
esp_err_t scd4x_init(const scd4x_config_t *config);
