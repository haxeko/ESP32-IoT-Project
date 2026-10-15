/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/ld2410.h>

// Options of the "Range" select (farthest detection gate) in Home Assistant:
// each option is a Mode Select mode, the gate number. Farthest gates that
// differ between moving and still targets (set with the HLK tool) are shown as
// one extra option with mode RADAR_MODE_CUSTOM.

#define RADAR_MODE_CUSTOM 100

// "45 s", "2 min", "1 min 30 s"
void radar_format_duration(uint16_t seconds, char *buf, size_t len);

// "1.5 m", "2.25 m", "6 m"
void radar_format_distance(uint32_t cm, char *buf, size_t len);

// Radar settings shown in Home Assistant
typedef struct {
    ld2410_config_t config;   // farthest gates, no-one duration, thresholds
    uint16_t gate_cm;         // gate length: LD2410_DEFAULT_GATE_CM or LD2410_FINE_GATE_CM
    bool resolution_settable; // the radar answers the resolution query (LD2410B/C)
} radar_settings_t;

#define RADAR_KEEP (-1)

// Changes for radar_settings_session(), RADAR_KEEP: unchanged
typedef struct {
    int32_t duration_s; // no-one duration
    int32_t gate;       // farthest gate for moving and still targets
    int32_t gate_cm;    // gate length (restarts the radar)
} radar_settings_request_t;

// One configuration session with the radar: reads its settings, writes the
// requested ones and reads them back into *out. Waits for the radar's
// answers: a few seconds when it does not answer, about 2 s more when the
// gate length changes. ESP_ERR_NOT_SUPPORTED: a gate length change asked to a
// radar with a fixed 0.75 m resolution.
esp_err_t radar_settings_session(const radar_settings_request_t *want, radar_settings_t *out);
