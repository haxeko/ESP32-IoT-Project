/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Single-colour status LED driven from a 100 ms esp_timer (default: the blue LED of the ESP32-C3 SuperMini, GPIO8,
// on when the pin is low). From the highest priority:
//   identify       5 Hz blink                  a controller asks the device to identify itself
//   pairing        slow blink (1 s on, 1 s off) commissioning window open: waiting to be added to an app
//   sensor error   short flash every 2 s       the AHT21 or the ENS160 does not answer (wiring)
//   otherwise      off

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// gpio < 0: no LED (the states are kept, nothing is driven). The LED stays dark until a state is set.
esp_err_t status_led_init(int gpio, bool active_high);

void status_led_set_pairing(bool pairing);
void status_led_set_sensor_error(bool error);
// Identify: from START to STOP of the Identify cluster, or for ms (TriggerEffect command).
void status_led_set_identify(bool identify);
void status_led_identify_for(uint32_t ms);
