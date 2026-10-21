/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Keeps the Home Assistant "Wi-Fi RSSI" and "Uptime" entities up to date (diagnostic entities, disabled by default
// in Home Assistant). They come from the RSSI attribute of Wi-Fi Network Diagnostics and the UpTime attribute of
// General Diagnostics on endpoint 0. Both have the "changes omitted" quality: the Matter SDK sends them only when a
// controller subscribes, so Home Assistant would keep the RSSI of that moment, and after a restart of Home
// Assistant it computes the boot time from that old uptime. diag_report.cpp marks them changed, so the
// subscriptions send their current value with the next report.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Matter task, once the server is up. Checks every minute (CONFIG_DIAG_REPORT_RSSI_STEP_DB,
// CONFIG_DIAG_REPORT_REFRESH_MIN); nothing when both options are 0.
void diag_report_start();

// The decision taken every minute, apart from the Matter calls for the host tests.
struct diag_report_policy_t {
    int rssi_step_db;       // report the RSSI when it moved by this much since the last report (0: only refreshes)
    uint32_t refresh_ticks; // report the RSSI and the uptime every this many minutes (0: never)
    bool has_rssi;          // an RSSI was reported since the station connected
    int8_t last_rssi;       // the RSSI last reported
    uint32_t ticks;         // minutes since the last refresh
};

struct diag_report_due_t {
    bool rssi;
    bool up_time;
};

// connected: the station is connected to an access point and rssi is valid (else the RSSI attribute reads null and
// is not reported).
inline diag_report_due_t diag_report_tick(diag_report_policy_t &p, bool connected, int8_t rssi)
{
    diag_report_due_t due = { false, false };
    if (p.refresh_ticks > 0 && ++p.ticks >= p.refresh_ticks) {
        p.ticks = 0;
        due.up_time = true;
        due.rssi = connected;
    }
    if (!connected) {
        p.has_rssi = false;
        return due;
    }
    // After a (re)connection the first RSSI is reported too, so the next moves are measured from a value the
    // controllers have.
    if (p.rssi_step_db > 0 && (!p.has_rssi || abs(rssi - p.last_rssi) >= p.rssi_step_db)) {
        due.rssi = true;
    }
    if (due.rssi) {
        p.has_rssi = true;
        p.last_rssi = rssi;
    }
    return due;
}
