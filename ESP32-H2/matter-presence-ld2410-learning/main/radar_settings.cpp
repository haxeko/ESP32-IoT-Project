/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "radar_settings.h"

static const char *TAG = "radar_settings";

// The radar answers commands again about a second after a restart.
static constexpr uint32_t kRestartMs = 1000;

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

void radar_format_mac(const uint8_t mac[LD2410_MAC_LEN], char *buf, size_t len)
{
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

// In configuration mode. A radar without the resolution query (older
// firmware) uses 0.75 m gates.
static void read_gate_length(radar_settings_t *out)
{
    out->resolution_settable = ld2410_read_gate_length(&out->gate_cm) == ESP_OK;
    if (!out->resolution_settable) {
        out->gate_cm = LD2410_DEFAULT_GATE_CM;
    }
}

// In configuration mode. A radar without the MAC address query has no
// Bluetooth (LD2410 before the B/C versions). Asked again once when not
// answered: a missed answer would show "Not available" until the next session.
static void read_bluetooth(radar_settings_t *out)
{
    esp_err_t err = ld2410_read_bluetooth(&out->bluetooth_on, out->bt_mac);
    if (err == ESP_ERR_TIMEOUT) {
        err = ld2410_read_bluetooth(&out->bluetooth_on, out->bt_mac);
    }
    out->bluetooth_known = err == ESP_OK;
    if (!out->bluetooth_known) {
        out->bluetooth_on = false;
        memset(out->bt_mac, 0, sizeof(out->bt_mac));
    }
}

// Settings stored in the radar's flash that its next restart applies
typedef struct {
    bool gate_cm;
    bool bluetooth;
    uint16_t old_gate_cm;
    bool old_bluetooth;
} pending_t;

// In configuration mode: stores the requested gate length and Bluetooth state
// (bluetooth_on).
static esp_err_t store_changes(const radar_settings_request_t *want, const radar_settings_t *now, bool bluetooth_on,
                               pending_t *pending)
{
    pending->old_gate_cm = now->gate_cm;
    pending->old_bluetooth = now->bluetooth_on;
    const bool gate_cm_change = want->gate_cm != RADAR_KEEP && want->gate_cm != now->gate_cm;
    if (gate_cm_change && !now->resolution_settable) {
        ESP_LOGE(TAG, "This radar has a fixed 0.75 m resolution");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (want->bluetooth != RADAR_KEEP && !now->bluetooth_known) {
        ESP_LOGE(TAG, "The Bluetooth query failed (radar without Bluetooth?)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err;
    if (gate_cm_change) {
        err = ld2410_set_gate_length((uint16_t)want->gate_cm);
        if (err != ESP_OK) {
            return err;
        }
        pending->gate_cm = true;
    }
    if (bluetooth_on != now->bluetooth_on) {
        err = ld2410_set_bluetooth(bluetooth_on);
        if (err != ESP_OK) {
            return err;
        }
        pending->bluetooth = true;
    }
    return ESP_OK;
}

// In configuration mode: sets back the gate length and Bluetooth stored by
// store_changes().
static void undo_changes(const pending_t *pending)
{
    if (pending->gate_cm) {
        (void) ld2410_set_gate_length(pending->old_gate_cm);
    }
    if (pending->bluetooth) {
        (void) ld2410_set_bluetooth(pending->old_bluetooth);
    }
}

// After ld2410_restart(): waits for the radar, enters the configuration mode
// again and reads the gate length and Bluetooth back. *in_config tells
// whether the radar is in configuration mode afterwards.
static esp_err_t wait_restart(radar_settings_t *out, bool *in_config)
{
    *in_config = false;
    vTaskDelay(pdMS_TO_TICKS(kRestartMs));
    esp_err_t err = ld2410_config_begin();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "The radar does not answer after its restart");
        return err;
    }
    *in_config = true;
    read_gate_length(out);
    read_bluetooth(out);
    return ESP_OK;
}

// After the restart: the stored gate length and Bluetooth state are in use.
static esp_err_t check_applied(const radar_settings_request_t *want, bool bluetooth_on, const pending_t *pending,
                               const radar_settings_t *out)
{
    if (pending->gate_cm && out->gate_cm != want->gate_cm) {
        ESP_LOGE(TAG, "The radar uses %u cm gates after its restart instead of %u cm", out->gate_cm,
                 (unsigned)want->gate_cm);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (pending->bluetooth && out->bluetooth_on != bluetooth_on) {
        ESP_LOGE(TAG, "The radar Bluetooth is still %s after its restart", out->bluetooth_on ? "on" : "off");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t radar_settings_session(const radar_settings_request_t *want, radar_settings_t *out)
{
    esp_err_t err = ld2410_config_begin();
    if (err != ESP_OK) {
        return err;
    }
    bool in_config = true;
    read_gate_length(out);
    read_bluetooth(out);

    // Gate length and Bluetooth: stored first, then applied by one restart
    const bool bluetooth_on = want->bluetooth != RADAR_KEEP ? want->bluetooth != 0 : out->bluetooth_on;
    pending_t pending = {};
    err = store_changes(want, out, bluetooth_on, &pending);
    bool restarted = false;
    if (err == ESP_OK && (pending.gate_cm || pending.bluetooth)) {
        // The radar may restart before its answer is sent: only a refusal is an error.
        err = ld2410_restart();
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "The radar refused to restart, its settings are left unchanged");
        } else {
            restarted = true;
            err = wait_restart(out, &in_config);
        }
    }
    if (err != ESP_OK && !restarted) {
        undo_changes(&pending);
    }
    if (err == ESP_OK && restarted) {
        err = check_applied(want, bluetooth_on, &pending, out);
    }
    if (err == ESP_OK) {
        err = ld2410_read_config(&out->config);
    }
    if (err == ESP_OK && (want->duration_s != RADAR_KEEP || want->gate != RADAR_KEEP)) {
        err = write_settings(&out->config, want->duration_s, want->gate);
    }
    if (in_config && ld2410_config_end() != ESP_OK) {
        ESP_LOGW(TAG, "Could not end the configuration mode");
    }
    return err;
}
