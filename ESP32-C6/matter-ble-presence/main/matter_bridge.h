/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Two-chip version, Matter node (CONFIG_PRESENCE_ROLE_MATTER_UART): the node is a Matter bridge. Endpoint 1 is an
// Aggregator; each active slot of the scanner (enabled, beacon assigned) is a bridged device under it, an Occupancy
// Sensor named after the beacon's MAC address (NodeLabel, e.g. "C0:11:22:33:44:01"). The scanner sends the list
// (SLOTS frames, presence_link.cpp): a device is added when a slot becomes active, removed when it no longer is,
// and replaced (new endpoint, new accessory in the controller) when another beacon is assigned to the slot.
//
// esp-matter never reuses an endpoint ID (the next free one is kept in NVS). The table slot -> (endpoint, MAC) is
// kept in the NVS namespace "pbridge", so the same endpoints come back at every boot before the scanner speaks.
//
// Everything here runs with the CHIP stack lock held (the Matter task, or a caller holding the lock).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_matter.h>

#include "presence_link_proto.h"

// Before esp_matter::start()
esp_err_t matter_bridge_create_aggregator(esp_matter::node_t *node, uint32_t occupancy_feature);

// After esp_matter::start(): brings back the bridged devices of the table
void matter_bridge_restore(void);

// Applies the scanner's list. Returns true when the bridge matches it, false before matter_bridge_restore() or when
// a device could not be added (try again later). changed: slots whose device was added or replaced, also when false
// is returned (their occupancy is to be set again).
bool matter_bridge_sync(const plink_slots_t *want, uint8_t *changed);

// matter_bridge_restore() done: until then a slot without endpoint may still get one back
bool matter_bridge_ready(void);

// Endpoint of the slot's device, 0 if none
uint16_t matter_bridge_endpoint(int slot);

void matter_bridge_get(uint8_t *bridged_mask, uint16_t endpoint[PLINK_SLOT_COUNT]);

// Reachable attribute of every bridged device (false while the scanner is silent)
void matter_bridge_set_reachable(bool reachable);

// Forgets the table (factory reset)
void matter_bridge_erase(void);
