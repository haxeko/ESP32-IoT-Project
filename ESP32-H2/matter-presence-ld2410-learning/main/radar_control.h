/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

// LD2410 controls for Home Assistant, as Mode Select clusters: Home Assistant
// shows each one as a select entity named after the cluster Description,
// Apple Home does not show Mode Select.
//  - "Learn empty room" (Off / Learning) on the occupancy endpoint
//  - "Range": farthest detection gate (moving and still), on its own endpoint
//  - "Radar accuracy": gate length 0.75 m or 0.2 m (LD2410B/C), on its own
//    endpoint
// The delay before "unoccupied" is set with Matter HoldTime; the radar's own
// no-one duration is left as stored in the radar.
// One worker task sends all the radar commands, one session at a time.

// Before esp_matter::start().
esp_err_t radar_control_create(esp_matter::node_t *node, esp_matter::endpoint_t *occupancy_endpoint);

// After esp_matter::start(): starts the worker, which first reads the radar
// settings.
esp_err_t radar_control_start(void);

// PRE_UPDATE attribute callback, on the Matter task: handles the CurrentMode
// changes of the clusters above. An error rejects the change.
esp_err_t radar_control_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                   const esp_matter_attr_val_t *val);
