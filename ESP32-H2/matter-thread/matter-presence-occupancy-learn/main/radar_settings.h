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

// Options of the "Duration" (LD2410 no-one duration) and "Range" (farthest
// detection gate) selects in Home Assistant. Each option is a Mode Select
// mode: the option index for Duration, the gate number for Range. A radar
// value that matches no option (set with the HLK tool) is shown as one extra
// option with mode RADAR_MODE_CUSTOM.

#define RADAR_MODE_CUSTOM 100

typedef struct {
    uint16_t seconds;
    const char *label;
} radar_duration_option_t;

#define RADAR_DURATION_OPTION_COUNT 9
extern const radar_duration_option_t radar_duration_options[RADAR_DURATION_OPTION_COUNT];

// Option index of a no-one duration, -1 when it is not an option.
int radar_duration_option(uint16_t seconds);

// "45 s", "2 min", "1 min 30 s"
void radar_format_duration(uint16_t seconds, char *buf, size_t len);

// "1.5 m", "2.25 m", "6 m"
void radar_format_distance(uint32_t cm, char *buf, size_t len);

#define RADAR_KEEP (-1)

// One configuration session with the radar: reads its settings (and the gate
// length while *gate_cm is 0), writes the requested no-one duration and
// farthest gate for moving and still targets (RADAR_KEEP: unchanged), then
// reads them back into *out. Waits for the radar's answers, up to a few
// seconds when it does not answer.
esp_err_t radar_settings_session(int32_t want_duration_s, int32_t want_gate, uint16_t *gate_cm,
                                 ld2410_config_t *out);
