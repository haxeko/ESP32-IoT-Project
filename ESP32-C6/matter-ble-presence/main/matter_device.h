/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// The Matter node (NORMAL mode only): root node + five Occupancy Sensor endpoints, Matter over Thread.
//
// Endpoints: 0 root node, 1-5 "Presence 1".."Presence 5", created in this order before esp_matter::start() so
// their ids never change (esp-matter numbers endpoints in creation order; the id it keeps in NVS only applies to
// endpoints created after start). They always exist, whatever the beacon configuration. Each has Identify and
// Occupancy Sensing with the RFSensing feature (Kconfig PRESENCE_OCCUPANCY_FEATURE), no HoldTime attribute (the
// absence timeout is the firmware's, per slot), and a Descriptor TagList (Common Number namespace, tag 1-5) that
// tells the five sibling endpoints apart.
//
// Thread: MTD build, set to Minimal End Device (receiver on when idle, mains powered): never a router.
// The scanner is paused while the radio belongs to Matter: not commissioned, commissioning session, CHIPoBLE
// connection, Thread detached, and for CONFIG_PRESENCE_SCAN_RESUME_DELAY_SEC after attaching / commissioning.

#pragma once

#include <stdbool.h>

#include <esp_err.h>

esp_err_t matter_device_start(void);

// Occupancy attribute of a slot's endpoint, from the presence task. Takes the Matter stack lock; the cluster only
// reports (subscriptions, OccupancyChanged event) when the value changes. Returns false if Matter is not up yet.
bool matter_device_set_occupancy(int slot, bool occupied);

// Thread role, link mode, parent RSSI and MAC / MLE counters, for the diagnostics log
void matter_device_log_diagnostics(void);
