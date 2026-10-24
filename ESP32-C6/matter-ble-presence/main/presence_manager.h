/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// NORMAL mode presence: matches scanner reports to the five slots and drives presence_logic.
//
//   NimBLE host task: presence_manager_filter() drops every address that no active slot has (before the frame is
//   even parsed); presence_manager_sink() keeps valid iBeacon frames of a slot's address and type, and forwards at
//   most one per slot every CONFIG_PRESENCE_REPORT_THROTTLE_MS (strongest RSSI of the period) to a bounded queue,
//   never blocking (a full queue counts a drop).
//   presence task: applies the packets, the timeouts (scan clock), and reports each change through report(),
//   outside any lock of its own. A report that cannot be delivered (Matter not started yet) is retried until it is.
//
// The slot table is built once at start-up from the stored configuration and never changes in NORMAL mode (a new
// configuration takes a restart), so the host task reads it without locking.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#include "ble_scanner.h"
#include "presence_config.h"

// Delivers a presence change of a slot; returns false when it could not be delivered (retried later)
typedef bool (*presence_report_fn_t)(int slot, bool present);

esp_err_t presence_manager_start(const presence_config_t *cfg, presence_report_fn_t report);

// ble_scanner callbacks (NimBLE host task)
bool presence_manager_filter(const uint8_t addr[BLE_ADDR_LEN], uint8_t addr_type);
void presence_manager_sink(const ble_adv_report_t *report);

typedef struct {
    bool active;
    bool present;
    bool reported; // Matter attribute equals present
    int8_t last_rssi;
    uint32_t age_s; // scan clock since the last valid packet, 0 if never seen
    uint32_t matched; // iBeacon frames of the slot's beacon seen by the host task
    uint32_t accepted;
    uint32_t rssi_rejected;
} presence_slot_stats_t;

typedef struct {
    presence_slot_stats_t slots[PRESENCE_SLOT_COUNT];
    uint32_t queue_drops;
    uint32_t report_retries;
} presence_manager_stats_t;

void presence_manager_get_stats(presence_manager_stats_t *out);
