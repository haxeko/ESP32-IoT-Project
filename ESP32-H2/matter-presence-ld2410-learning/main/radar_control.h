/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <sdkconfig.h>

// LD2410 controls for Home Assistant, as Mode Select clusters: Home Assistant
// shows each one as a select entity named after the cluster Description,
// Apple Home does not show Mode Select.
//  - "Learn empty room" (Off / Learning / Factory thresholds) on the occupancy
//    endpoint: Factory thresholds writes the LD2410 factory thresholds back
//    (quiet_learn_restore_factory()), then the select shows Off again
//  - "Range": farthest detection gate (moving and still), on its own endpoint
//  - "Radar resolution": gate length 0.75 m or 0.2 m (LD2410B/C), on its own
//    endpoint
//  - "Radar Bluetooth": Off / On (LD2410B/C, HLKRadarTool app), on its own
//    endpoint created after the others. Off until it is turned on here (the
//    choice is kept in NVS and applied at boot). The radar's password is left
//    as it is
// The delay before "unoccupied" is set with Matter HoldTime; the radar's own
// no-one duration is left as stored in the radar.
// One worker task sends all the radar commands, one session at a time.

// Before esp_matter::start().
esp_err_t radar_control_create(esp_matter::node_t *node, esp_matter::endpoint_t *occupancy_endpoint);

// After esp_matter::start() (reads NVS): starts the worker, which first reads
// the radar settings.
esp_err_t radar_control_start(void);

// Starts the empty room learning like "Learning" chosen in Home Assistant
// (triple click on the button). Does not wait, so it may run in an esp_timer
// callback. False when the worker is not running (the status LED shows a
// failure) or a learning is already requested or running. The status LED
// shows the learning and its result (status_led.h).
bool radar_control_request_learning(void);

// Writes the LD2410 factory thresholds like "Factory thresholds" chosen in Home
// Assistant (four clicks on the button): a learning requested or running is
// cancelled first. Does not wait, so it may run in an esp_timer callback.
// False when the worker is not running (the status LED shows a failure). The
// status LED blinks purple once they are written, or shows red (status_led.h).
bool radar_control_request_factory_thresholds(void);

#if CONFIG_PRESENCE_ESP_ENGINE
// LEARN_CANCEL on the console of the ESP32 presence engine, like "Off" chosen
// in Home Assistant: cancels a learning requested or running (the thresholds
// stay as they were) and Factory thresholds not written yet. Does not wait.
void radar_control_cancel_learning(void);
#endif

// PRE_UPDATE attribute callback, on the Matter task: handles the CurrentMode
// changes of the clusters above. An error rejects the change.
esp_err_t radar_control_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                   const esp_matter_attr_val_t *val);
