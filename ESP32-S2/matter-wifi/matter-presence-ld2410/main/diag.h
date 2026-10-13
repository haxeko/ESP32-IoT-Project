/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stddef.h>

// Keeps the reasons of the last resets in NVS and reads the summary of the
// last crash from the core dump partition, so that resets can be diagnosed
// without a serial console (log, setup page, or an esptool flash read).
// Call once after nvs_flash_init().
void diag_record_boot(void);

// True when the previous reset was caused by a supply voltage dip.
bool diag_last_reset_was_brownout(void);

// Human-readable report: boot count, last reset reasons and last crash.
void diag_get_report(char *buf, size_t size);
