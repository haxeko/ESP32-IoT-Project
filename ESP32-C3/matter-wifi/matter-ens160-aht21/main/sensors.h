/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Sensor task: reads the AHT21 and the ENS160 of the module on one I2C bus every CONFIG_SENSOR_PERIOD_SEC, gives the
// ENS160 the temperature and humidity of the AHT21 for its compensation, and hands the values to a callback. It runs
// without Matter (before commissioning, without Wi-Fi). A sensor that stops answering keeps its last values for
// kMaxFailures readings, then has none until it answers again: it is searched for at every reading, so a module
// plugged in while the device runs is found.

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // AHT21. Not valid: not found, or no answer (the values below are not set).
    bool climate_valid;
    float temperature_c; // CONFIG_SENSOR_TEMPERATURE_OFFSET applied
    float humidity_pct;  // relative humidity of the same air at that temperature
    // ENS160. Found: it answers (in its first second it has no values yet). Not valid: no values, or values not valid
    // (yet), see CONFIG_ENS160_REPORT_WHILE_WARMING_UP (the values below are not set).
    bool air_found;
    bool air_valid;
    uint8_t aqi;       // AQI-UBA 1 (excellent) to 5 (unhealthy)
    uint16_t tvoc_ppb; // 0 to 65000 ppb
    uint16_t eco2_ppm; // 400 to 65000 ppm
} sensor_values_t;

// Runs in the sensor task after each reading.
typedef void (*sensor_values_cb_t)(const sensor_values_t *values);

// Starts the I2C bus (CONFIG_SENSOR_I2C_SDA_GPIO, CONFIG_SENSOR_I2C_SCL_GPIO) and the sensor task.
esp_err_t sensors_start(sensor_values_cb_t cb);

// One reading of both sensors: the body of the sensor task loop, also used by the host tests. After
// sensors_init_bus().
esp_err_t sensors_init_bus(void);
void sensors_read_once(sensor_values_t *values);
// Log line of a reading, written by the task at the first reading and then once a minute.
void sensors_log(const sensor_values_t *values);
