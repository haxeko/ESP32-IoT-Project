/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Connection watchdog: brings a commissioned device back when no controller subscribes to it any more.
//
// A controller that loses its subscription (Wi-Fi drop, device unreachable) tries again on its own schedule: the
// matter.js Matter Server of Home Assistant waits for a DNS-SD announcement of the device or tries again every
// 10 minutes, and the Matter SDK resumes a lost subscription from the device side only after 5 minutes, then less and
// less often (up to 6 hours). While a commissioned device has no subscription at all, this watchdog
//   - announces it again on DNS-SD every 5 minutes, so the controllers find it as soon as it can be reached,
//   - reconnects Wi-Fi every CONFIG_NET_WATCHDOG_RECONNECT_MIN minutes (new scan: strongest access point, new
//     address lease, new announcements),
//   - restarts the device after CONFIG_NET_WATCHDOG_RESTART_MIN minutes.
// It also restarts the device when the Matter task has not run for 3 minutes (stuck or starved), which the task
// watchdog of ESP-IDF does not do in this configuration.

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// After esp_matter::start(). Checks every 30 s from an esp_timer; logs once if the previous restart was its own.
esp_err_t net_watchdog_start();

// The decision taken at each check, apart from the Matter calls for the host tests. Times in seconds.
enum net_watchdog_action_t : uint8_t {
    NET_WD_NONE,
    NET_WD_READVERTISE, // announce the device again on DNS-SD
    NET_WD_RECONNECT,   // disconnect Wi-Fi, the Matter stack connects again at once
    NET_WD_RESTART,
};

struct net_watchdog_policy_t {
    uint32_t readvertise_s; // without a subscription, announce again every this many seconds (0: never)
    uint32_t reconnect_s;   // ... reconnect Wi-Fi every this many seconds (0: never)
    uint32_t restart_s;     // ... restart after this many seconds (0: never)
    bool idle;              // commissioned and no subscription since idle_since
    uint32_t idle_since;
    uint32_t next_readvertise;
    uint32_t next_reconnect;
};

inline net_watchdog_action_t net_watchdog_step(net_watchdog_policy_t &p, uint32_t now, bool commissioned,
                                               bool subscribed)
{
    if (!commissioned || subscribed) {
        p.idle = false;
        return NET_WD_NONE;
    }
    if (!p.idle) {
        p.idle = true;
        p.idle_since = now;
        p.next_readvertise = now + p.readvertise_s;
        p.next_reconnect = now + p.reconnect_s;
        return NET_WD_NONE;
    }
    if (p.restart_s > 0 && now - p.idle_since >= p.restart_s) {
        return NET_WD_RESTART;
    }
    if (p.reconnect_s > 0 && now >= p.next_reconnect) {
        p.next_reconnect = now + p.reconnect_s;
        p.next_readvertise = now + p.readvertise_s; // reconnecting announces the device again
        return NET_WD_RECONNECT;
    }
    if (p.readvertise_s > 0 && now >= p.next_readvertise) {
        p.next_readvertise = now + p.readvertise_s;
        return NET_WD_READVERTISE;
    }
    return NET_WD_NONE;
}

// The Matter task last ran the watchdog check at heartbeat: stuck when that is limit_s or more ago.
inline bool net_watchdog_stuck(uint32_t now, uint32_t heartbeat, uint32_t limit_s)
{
    return now - heartbeat >= limit_s;
}
