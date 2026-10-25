/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// UART link of the two-chip version (frames: presence_link_proto.h), one task on each chip:
//
//   scanner (ESP32-C3)      sends a STATE frame every second and within 100 ms of a presence change; in the CONFIG
//                           mode it keeps sending (mode CONFIG) so the Matter node knows it is alive. After a
//                           restart a slot is "unknown" until its beacon is heard or a whole absence timeout of
//                           scanning has passed (plink_known_mask()).
//   Matter node (ESP32-H2)  applies the known states of the STATE frames to the five Occupancy Sensors (only
//                           changes, and again until Matter accepts them) and keeps the others, sends a STATUS frame
//                           every second, and turns every sensor off when the scanner stays silent for
//                           CONFIG_PRESENCE_LINK_TIMEOUT_SEC.
//
// Pins, UART and baud rate: Kconfig "Two-chip UART link". The console of both chips must not use those pins
// (sdkconfig.defaults of the two projects: console on USB-Serial-JTAG only).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#include "presence_config.h"
#include "presence_link_proto.h"

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t crc_errors;
    uint32_t bad_frames;
    int64_t last_rx_ms; // esp_timer time of the last valid frame from the other chip, 0: none yet
    bool have_peer_state;
    plink_state_t peer_state; // Matter node: last STATE received
    bool have_peer_status;
    plink_status_t peer_status; // scanner: last STATUS received
    uint32_t link_losses;       // Matter node: times the scanner was declared gone
} presence_link_stats_t;

// Scanner side. cfg: the configuration presence_manager runs with (NORMAL mode), nullptr in CONFIG mode.
esp_err_t presence_link_start_scanner(bool config_mode, const presence_config_t *cfg);
// presence_report_fn_t of the scanner: records the state, the link task sends it. Never blocks, always delivered.
bool presence_link_report(int slot, bool present);

// Matter node side: apply(slot, occupied) returns false when Matter cannot take the value yet (retried);
// status() fills the STATUS frame (called from the link task).
typedef bool (*presence_link_apply_fn_t)(int slot, bool occupied);
typedef void (*presence_link_status_fn_t)(plink_status_t *status);
esp_err_t presence_link_start_matter(presence_link_apply_fn_t apply, presence_link_status_fn_t status);

void presence_link_get_stats(presence_link_stats_t *out);
// One line for the diagnostics log
void presence_link_log_diagnostics(void);
