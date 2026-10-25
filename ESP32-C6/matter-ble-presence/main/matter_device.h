/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// The Matter node (NORMAL mode only): root node + five Occupancy Sensor endpoints, Matter over Thread (ESP32-C6
// version) or over Wi-Fi (ESP32-C3 version).
//
// Endpoints: 0 root node, 1-5 "Presence 1".."Presence 5", created in this order before esp_matter::start() so
// their ids never change (esp-matter numbers endpoints in creation order; the id it keeps in NVS only applies to
// endpoints created after start). They always exist, whatever the beacon configuration. Each has Identify and
// Occupancy Sensing with the RFSensing feature (Kconfig PRESENCE_OCCUPANCY_FEATURE), no HoldTime attribute (the
// absence timeout is the firmware's, per slot), and a Descriptor TagList (Common Number namespace, tag 1-5) that
// tells the five sibling endpoints apart.
//
// Thread: MTD build, set to Minimal End Device (receiver on when idle, mains powered): never a router.
// Wi-Fi: station of the network given at commissioning (credentials kept by the Wi-Fi driver in "nvs").
// The scanner is paused while the radio belongs to Matter: not commissioned, commissioning session, CHIPoBLE
// connection, network down (Thread detached / Wi-Fi disconnected), and for CONFIG_PRESENCE_SCAN_RESUME_DELAY_SEC
// after the network comes up / commissioning ends.

#pragma once

#include <stdbool.h>

#include <esp_err.h>

#include "sdkconfig.h"

#if CONFIG_ENABLE_MATTER_OVER_THREAD
#define MATTER_DEVICE_NETWORK_NAME "Thread"
#define MATTER_DEVICE_NETWORK_ID "thread" // for the configuration page
#else
#define MATTER_DEVICE_NETWORK_NAME "Wi-Fi"
#define MATTER_DEVICE_NETWORK_ID "wifi"
#endif

esp_err_t matter_device_start(void);

// Occupancy attribute of a slot's endpoint, from the presence task. Takes the Matter stack lock; the cluster only
// reports (subscriptions, OccupancyChanged event) when the value changes. Returns false if Matter is not up yet.
bool matter_device_set_occupancy(int slot, bool occupied);

// For the diagnostics log: Thread role, link mode, parent RSSI and MAC / MLE counters, or Wi-Fi signal, channel and
// disconnections
void matter_device_log_diagnostics(void);
