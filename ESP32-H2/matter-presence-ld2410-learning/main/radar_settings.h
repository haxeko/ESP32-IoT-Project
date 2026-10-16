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

// "8F:27:2E:B8:0F:65"
void radar_format_mac(const uint8_t mac[LD2410_MAC_LEN], char *buf, size_t len);

// Radar settings shown in Home Assistant
typedef struct {
    ld2410_config_t config;   // farthest gates, no-one duration, thresholds
    uint16_t gate_cm;         // gate length: LD2410_DEFAULT_GATE_CM or LD2410_FINE_GATE_CM
    bool resolution_settable; // the radar answers the resolution query (LD2410B/C)
    bool bluetooth_known;     // the radar answers the Bluetooth (MAC address) query (LD2410B/C)
    bool bluetooth_on;
    uint8_t bt_mac[LD2410_MAC_LEN]; // Bluetooth MAC address when bluetooth_on
    bool bt_password_set;     // this session stored the requested password and restarted the radar
} radar_settings_t;

#define RADAR_KEEP (-1)

// Changes for radar_settings_session(), RADAR_KEEP / nullptr: unchanged
typedef struct radar_settings_request {
    int32_t duration_s = RADAR_KEEP; // no-one duration
    int32_t gate = RADAR_KEEP;       // farthest gate for moving and still targets
    int32_t gate_cm = RADAR_KEEP;    // gate length (restarts the radar)
    int32_t bluetooth = RADAR_KEEP;  // 0 off, 1 on (restarts the radar)
    // LD2410_BT_PASSWORD_LEN characters, stored when the radar's Bluetooth is
    // or becomes on (restarts the radar)
    const char *bt_password = nullptr;
} radar_settings_request_t;

// One configuration session with the radar: reads its settings, writes the
// requested ones and reads them back into *out. The gate length, Bluetooth
// and its password are applied together by one restart of the radar; when a
// step before the restart fails or the restart is refused, the gate length
// and Bluetooth already stored are set back. Waits for the radar's answers: a
// few seconds when it does not answer, about 2 s more for a restart.
// ESP_ERR_NOT_SUPPORTED: a gate length change asked to a radar with a fixed
// 0.75 m resolution, or a Bluetooth change to a radar that does not answer the
// Bluetooth query (no Bluetooth). A password the radar refuses or does not
// answer is skipped (bt_password_set stays false), the other settings are
// still applied.
esp_err_t radar_settings_session(const radar_settings_request_t *want, radar_settings_t *out);
