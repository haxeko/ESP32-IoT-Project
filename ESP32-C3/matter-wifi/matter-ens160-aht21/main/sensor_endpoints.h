/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter endpoints of the sensors, after the root node (endpoint 0). Their numbers follow the creation order and must
// not change: controllers that paired keep them across OTA updates.
//   1 Temperature Sensor (0x0302): Temperature Measurement, from the AHT21
//   2 Humidity Sensor (0x0307): Relative Humidity Measurement, from the AHT21
//   3 Air Quality Sensor (0x002C): Air Quality (from the AQI-UBA index of the ENS160), TVOC Concentration
//     Measurement and, with CONFIG_SENSOR_ECO2_AS_CO2, Carbon Dioxide Concentration Measurement (the eCO2 of the
//     ENS160, a CO2 estimated from the VOCs)
// A value the sensors do not have (not wired, warming up) is null, and the air quality Unknown.

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

#include "sensors.h"

// Before esp_matter::start().
esp_err_t sensor_endpoints_create(esp_matter::node_t *node);

// Sensor task, once Matter has started. Writes the values that changed (by more than the noise of the sensor, see
// sensor_endpoints.cpp); the clusters report them to the subscribed controllers. A value not written (Matter not
// ready yet) is written at the next reading.
void sensor_endpoints_update(const sensor_values_t *values);
