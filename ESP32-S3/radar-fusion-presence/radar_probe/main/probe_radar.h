/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// One task per radar UART: timestamps and records every frame, tells which
// radar is wired to the UART from its reports, and runs the configuration jobs
// the console asks for. This is a Phase 1 test tool, not the product driver:
// it records what the radars send and does no presence detection.

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROBE_CHANNELS 2
#define PROBE_JOB_VALUE_MAX 32

typedef enum {
    PROBE_RADAR_UNKNOWN = 0,
    PROBE_RADAR_LD2410,
    PROBE_RADAR_LD2450,
} probe_radar_type_t;

typedef enum {
    PROBE_JOB_INFO,        // read-only queries of the detected radar
    PROBE_JOB_INFO_ALL,    // every read-only query of both radar types: which ones does this firmware answer?
    PROBE_JOB_ENGINEERING, // arg 1 on, 0 off (LD2410, lost when the radar restarts)
    PROBE_JOB_TRACKING,    // arg LD2450_TRACKING_SINGLE / LD2450_TRACKING_MULTI (stored in the radar)
    PROBE_JOB_BLUETOOTH,   // arg 1 on, 0 off; stored, the radar restarts
    PROBE_JOB_RESTART,
    PROBE_JOB_SCAN,     // look for the radar on every baud rate it offers
    PROBE_JOB_SET_BAUD, // arg: baud rate stored in the radar, which restarts; the UART follows
    PROBE_JOB_COMMAND,  // arg: any command word, sent between enable and end configuration
    PROBE_JOB_POWERED,  // the radar was just powered on: set engineering mode again once it runs
} probe_job_t;

// Starts the task of channel index (0 or 1) on a UART; the records name the
// channel by its UART number.
esp_err_t probe_radar_start(int index, int uart, int rx_gpio, int tx_gpio, uint32_t baud);

// Queues a job for the channel task. False when the channel does not run or
// two jobs are waiting already.
bool probe_radar_job(int index, probe_job_t job, uint32_t arg, const uint8_t *value, size_t value_len);

// Channel by UART number ("1", "2") or radar ("2410", "ld2450", ...); -1 if none.
int probe_radar_find(const char *name);
probe_radar_type_t probe_radar_type(int index);
const char *probe_radar_type_name(probe_radar_type_t type);

// Engineering mode wanted on the LD2410, also set again after the radar restarts.
void probe_radar_want_engineering(bool on);
// ESP32 UART rate only (the radar keeps its own)
void probe_radar_set_baud(int index, uint32_t baud);
void probe_radar_set_dump(int index, bool on);
// @D / @A records of every frame (ACKs are always recorded)
void probe_radar_set_frames(bool on);

// Writes the @S record of the channel.
void probe_radar_stats(int index);
// Short text of the latest report, for the @H summary.
void probe_radar_summary(int index, char *buf, size_t len);
