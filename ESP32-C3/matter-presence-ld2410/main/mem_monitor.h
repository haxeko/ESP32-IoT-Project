/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdint.h>

// Logs (tag "mem") the free heap (now, lowest since boot, largest block) and the unused stack of the main tasks
// (CHIP, wifi, esp_timer, presence, ld2410, radar_cfg, id_diag, and console with the Matter shell) now and then every
// period_s seconds (0: only now). A warning when a stack has less than 512 bytes left, and once for each new lowest
// free heap below 16 KB, with the time window it was reached in. Runs in the esp_timer task.
esp_err_t mem_monitor_start(uint32_t period_s);
