/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdio.h>

#include <algorithm>

#include "radar_settings.h"

static const char *TAG = "radar_settings";

// The radar keeps reporting a target for this long after losing it; Matter
// HoldTime then adds its own delay before the room turns unoccupied. The
// LD2410 factory default is 5 s.
const radar_duration_option_t radar_duration_options[RADAR_DURATION_OPTION_COUNT] = {
    { 0, "0 s" },     { 5, "5 s" },     { 10, "10 s" },   { 15, "15 s" },   { 30, "30 s" },
    { 60, "1 min" },  { 120, "2 min" }, { 300, "5 min" }, { 600, "10 min" },
};

int radar_duration_option(uint16_t seconds)
{
    for (int i = 0; i < RADAR_DURATION_OPTION_COUNT; i++) {
        if (radar_duration_options[i].seconds == seconds) {
            return i;
        }
    }
    return -1;
}

void radar_format_duration(uint16_t seconds, char *buf, size_t len)
{
    const unsigned min = seconds / 60;
    const unsigned sec = seconds % 60;
    if (min == 0) {
        snprintf(buf, len, "%u s", sec);
    } else if (sec == 0) {
        snprintf(buf, len, "%u min", min);
    } else {
        snprintf(buf, len, "%u min %u s", min, sec);
    }
}

void radar_format_distance(uint32_t cm, char *buf, size_t len)
{
    const unsigned m = cm / 100;
    const unsigned frac = cm % 100;
    if (frac == 0) {
        snprintf(buf, len, "%u m", m);
    } else if (frac % 10 == 0) {
        snprintf(buf, len, "%u.%u m", m, frac / 10);
    } else {
        snprintf(buf, len, "%u.%02u m", m, frac);
    }
}

// Gate numbers the radar accepts
static uint8_t valid_gate(uint8_t gate)
{
    return std::clamp<uint8_t>(gate, LD2410_MIN_MAX_GATE, LD2410_MAX_MAX_GATE);
}

// In configuration mode: writes the requested settings and reads them back
// into cfg.
static esp_err_t write_settings(ld2410_config_t *cfg, int32_t want_duration_s, int32_t want_gate)
{
    const uint16_t duration = want_duration_s != RADAR_KEEP ? (uint16_t)want_duration_s : cfg->no_one_duration_s;
    const uint8_t moving = want_gate != RADAR_KEEP ? (uint8_t)want_gate : valid_gate(cfg->max_moving_gate);
    const uint8_t still = want_gate != RADAR_KEEP ? (uint8_t)want_gate : valid_gate(cfg->max_still_gate);
    if (duration == cfg->no_one_duration_s && moving == cfg->max_moving_gate && still == cfg->max_still_gate) {
        return ESP_OK;
    }
    esp_err_t err = ld2410_set_max_gates_and_duration(moving, still, duration);
    if (err == ESP_OK) {
        err = ld2410_read_config(cfg);
    }
    if (err == ESP_OK &&
        (cfg->no_one_duration_s != duration || cfg->max_moving_gate != moving || cfg->max_still_gate != still)) {
        ESP_LOGE(TAG, "The radar did not keep the new settings");
        err = ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t radar_settings_session(int32_t want_duration_s, int32_t want_gate, uint16_t *gate_cm,
                                 ld2410_config_t *out)
{
    esp_err_t err = ld2410_config_begin();
    if (err != ESP_OK) {
        return err;
    }
    if (*gate_cm == 0 && ld2410_read_gate_length(gate_cm) != ESP_OK) {
        ESP_LOGI(TAG, "No distance resolution query on this radar, using %u cm gates", LD2410_DEFAULT_GATE_CM);
        *gate_cm = LD2410_DEFAULT_GATE_CM;
    }
    err = ld2410_read_config(out);
    if (err == ESP_OK && (want_duration_s != RADAR_KEEP || want_gate != RADAR_KEEP)) {
        err = write_settings(out, want_duration_s, want_gate);
    }
    if (ld2410_config_end() != ESP_OK) {
        ESP_LOGW(TAG, "Could not end the configuration mode");
    }
    return err;
}
