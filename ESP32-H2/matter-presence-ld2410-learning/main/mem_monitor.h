/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdint.h>

// Logs the free heap (now, lowest since boot, largest block) and the unused
// stack of the main tasks once now and then every period_s seconds, as a
// warning when one runs low. Runs in the esp_timer task.
esp_err_t mem_monitor_start(uint32_t period_s);
