/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// LD2410 controls for Home Assistant, as Mode Select clusters: Home Assistant shows each one as a select entity
// named after the cluster Description and sorts them by name (Hold time, Learn empty room, Radar Bluetooth, Radar
// resolution, Range); Apple Home does not show Mode Select.
//  - "Learn empty room" (Off / Learning / Factory thresholds) on the occupancy endpoint (1)
//  - "Range": farthest detection gate (moving and still), endpoint 2
//  - "Radar resolution": gate length 0.75 m or 0.2 m (LD2410B/C), endpoint 3
//  - "Radar Bluetooth": Off / On (LD2410B/C, HLKRadarTool app), endpoint 4, created last
// The delay before "unoccupied" is set with Matter HoldTime; the radar's own no-one duration is left as stored in
// the radar. The radar_cfg task (radar_worker.cpp) does the work.

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

// Before esp_matter::start(): the Mode Select clusters and endpoints.
esp_err_t radar_control_create(esp_matter::node_t *node, esp_matter::endpoint_t *occupancy_endpoint);

// After esp_matter::start() (reads NVS): starts the radar_cfg task, which first reads the radar settings.
esp_err_t radar_control_start(void);

// PRE_UPDATE attribute callback, on the Matter task: hands the CurrentMode changes of the clusters above to the
// radar_cfg task. An error rejects the change.
esp_err_t radar_control_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                   const esp_matter_attr_val_t *val);
