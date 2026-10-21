/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>

// Built only with CONFIG_PRESENCE_ENERGY_FILTER
#if CONFIG_PRESENCE_ENERGY_FILTER

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "energy_filter.h"

// Reports without energies for this long after the last one: the radar is not
// sending, nothing to turn on
static constexpr int64_t kFreshMs = 2000;

static portMUX_TYPE s_filter_lock = portMUX_INITIALIZER_UNLOCKED;
// s_filter_lock
static bool s_configured;
static energy_filter_config_t s_cfg;
static energy_filter_state_t s_state;
static energy_filter_state_t s_event; // last event, for the log
static bool s_event_pending;
static bool s_seen; // a report was received
static bool s_last_engineering;
static int64_t s_last_report_ms;

// Reader task of the LD2410 driver, for every report
static void on_report(const ld2410_report_t *report)
{
    taskENTER_CRITICAL(&s_filter_lock);
    s_seen = true;
    s_last_engineering = report->engineering;
    s_last_report_ms = report->target.timestamp_ms;
    if (s_configured) {
        energy_filter_step(&s_state, &s_cfg, report);
        if (s_state.event != ENERGY_FILTER_NO_EVENT) {
            s_event = s_state;
            s_event_pending = true;
        }
    }
    taskEXIT_CRITICAL(&s_filter_lock);
}

void energy_filter_start(void)
{
    ld2410_set_report_hook(on_report);
}

void energy_filter_configure(const radar_settings_t *settings)
{
    energy_filter_config_t cfg = {};
    memcpy(cfg.moving_threshold, settings->config.thresholds.moving, sizeof(cfg.moving_threshold));
    memcpy(cfg.still_threshold, settings->config.thresholds.still, sizeof(cfg.still_threshold));
    cfg.max_moving_gate = settings->config.max_moving_gate;
    cfg.max_still_gate = settings->config.max_still_gate;
    cfg.gate_cm = settings->gate_cm;
    cfg.max_cm = CONFIG_PRESENCE_MAX_DISTANCE_CM;
    cfg.strong = CONFIG_PRESENCE_ENERGY_FILTER_STRONG;
    cfg.frames = CONFIG_PRESENCE_ENERGY_FILTER_FRAMES;

    taskENTER_CRITICAL(&s_filter_lock);
    s_cfg = cfg;
    if (!s_configured) {
        s_state = {};
        s_configured = true;
    }
    taskEXIT_CRITICAL(&s_filter_lock);
}

bool energy_filter_presence(bool *present)
{
    taskENTER_CRITICAL(&s_filter_lock);
    const bool active = s_configured;
    *present = s_state.present;
    taskEXIT_CRITICAL(&s_filter_lock);
    return active;
}

bool energy_filter_take_event(energy_filter_state_t *out)
{
    taskENTER_CRITICAL(&s_filter_lock);
    const bool pending = s_event_pending;
    if (pending) {
        *out = s_event;
        s_event_pending = false;
    }
    taskEXIT_CRITICAL(&s_filter_lock);
    return pending;
}

bool energy_filter_needs_engineering(int64_t now_ms)
{
    taskENTER_CRITICAL(&s_filter_lock);
    const bool need = s_seen && !s_last_engineering && now_ms - s_last_report_ms <= kFreshMs;
    taskEXIT_CRITICAL(&s_filter_lock);
    return need;
}

#endif // CONFIG_PRESENCE_ENERGY_FILTER
