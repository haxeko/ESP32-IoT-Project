/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Concentrations are in µg/m³. A value is NAN when the sensor does not
// measure it (e.g. PM1.0 on the SDS011).
typedef struct {
    bool valid;     // false when the sensor stopped sending data
    float pm1_0;
    float pm2_5;
    float pm10;
} pm_sensor_data_t;

typedef void (*pm_sensor_cb_t)(const pm_sensor_data_t *data, void *user_data);

typedef struct {
    int rx_pin;
    int tx_pin;
    uint32_t report_interval_ms;    // continuous mode: samples are averaged and reported at this
                                    // interval (the PMS duty cycle reports once per period)
    pm_sensor_cb_t cb;              // called from the sensor task
    void *user_data;
} pm_sensor_config_t;

esp_err_t pm_sensor_init(const pm_sensor_config_t *config);
