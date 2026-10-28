/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>

// The I2C master bus shared by the OLED and the CO2 sensor: the ESP32-C3 has
// a single I2C controller. i2c_bus_init() is called once, before the tasks
// that use the bus start; i2c_bus_get() then returns the bus, or NULL if it
// could not be created. The driver serializes the transfers of the devices.
esp_err_t i2c_bus_init(int sda_pin, int scl_pin);
i2c_master_bus_handle_t i2c_bus_get(void);
