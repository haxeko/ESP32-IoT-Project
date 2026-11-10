/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stddef.h>

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Name of the reason of the last reset (esp_reset_reason()), for the logs.
const char *app_diag_reset_reason(void);

// Diagnostics to find why commissioning or a sensor fails (CONFIG_APP_DIAG,
// off by default; the sdkconfig.diag overlay turns it on with a console on
// UART0). Without it, the functions below do nothing.

// Steps of a commissioning. The furthest one reached since boot is logged with
// each event and shown as "S<n>" on the OLED.
typedef enum {
    APP_DIAG_STAGE_NONE = -1,           // an event that is not a step
    APP_DIAG_STAGE_BOOT = 0,
    APP_DIAG_STAGE_BLE_ADVERTISING,     // 1: advertising over BLE, waiting for a commissioner
    APP_DIAG_STAGE_BLE_CONNECTED,       // 2: the commissioner connected over BLE
    APP_DIAG_STAGE_PASE,                // 3: PASE session established (pairing code accepted)
    APP_DIAG_STAGE_WIFI_CONNECTED,      // 4: joined the Wi-Fi network it was given
    APP_DIAG_STAGE_IP,                  // 5: got an IP address
    APP_DIAG_STAGE_CASE,                // 6: CASE session established over the network
    APP_DIAG_STAGE_COMMISSIONED,        // 7: commissioning complete
} app_diag_stage_t;

#if CONFIG_APP_DIAG

// Counts this boot by reset reason (the counters are kept in NVS) and logs
// them. Call once NVS is initialized.
void app_diag_init(void);

// Starts the periodic status log: heap, stack and CPU of every task, sensor
// and OLED transfer counters, Wi-Fi signal.
void app_diag_start(void);

// Logs an event of the commissioning with the heap, and remembers the furthest
// stage. Called from the Matter thread.
void app_diag_event(const char *what, app_diag_stage_t stage);

// Clears the reset counters. Can be called from any task.
void app_diag_clear(void);

// Short status for the OLED, e.g. "N3 B2 W0 P0 S4": resets counted (N), of
// which brown-outs (B), watchdog resets (W) and panics (P), and the furthest
// commissioning stage since boot (S).
void app_diag_status_line(char *buf, size_t size);

#else

static inline void app_diag_init(void) {}
static inline void app_diag_start(void) {}
static inline void app_diag_event(const char *what, app_diag_stage_t stage) {}
static inline void app_diag_clear(void) {}
static inline void app_diag_status_line(char *buf, size_t size) {}

#endif

#ifdef __cplusplus
}
#endif
