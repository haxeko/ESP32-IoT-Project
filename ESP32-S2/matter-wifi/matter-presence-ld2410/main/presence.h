/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// Radar -> presence decisions. Plain C, also built by the host tests.

#include <stdbool.h>
#include <stdint.h>

#include "ld2410_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// True while the radar data is recent enough to trust: the last frame is at
// most timeout_ms old. Before the first frame the time since boot counts, so
// the radar gets the same grace period at start-up.
bool presence_radar_fresh(bool has_frame, int64_t last_frame_ms, int64_t now_ms, int64_t timeout_ms);

// Someone is detected in the report. With max_distance_cm > 0, targets
// further away are ignored; 0 relies on the radar's own gate settings.
bool presence_detected(const ld2410_report_t *report, unsigned max_distance_cm);

#ifdef __cplusplus
}
#endif
