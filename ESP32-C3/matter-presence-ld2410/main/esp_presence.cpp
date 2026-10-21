/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>

// Built only with CONFIG_PRESENCE_ESP_ENGINE
#if CONFIG_PRESENCE_ESP_ENGINE

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>

#include <drivers/ld2410.h>
#include <drivers/status_led.h>

#include "esp_presence.h"
#include "presence_engine.h"
#include "radar_worker.h"

static const char *TAG = "esp_presence";

static constexpr uint32_t kTaskStack = 4096;
// Above the presence and radar_cfg tasks (2), below the LD2410 reader (5) and the Wi-Fi tasks
static constexpr UBaseType_t kTaskPriority = 3;
static constexpr UBaseType_t kFrameQueueLength = 20; // 2 s of reports
static constexpr UBaseType_t kCommandQueueLength = 8;
static constexpr uint32_t kLoopMs = 100;
static constexpr uint32_t kDiagStreamMs = 1000;
// Reports without energies for this long: the engineering mode must be turned on (also right after boot)
static constexpr uint32_t kEngineeringCheckMs = 2000;

static constexpr const char *kNvsNamespace = "esp_presence";
static constexpr const char *kNvsProfileKey = "profile";

// Engine task only, after esp_presence_start()
static pe_engine_t s_engine;
static QueueHandle_t s_frames;   // pe_frame_t, from the LD2410 reader task
static QueueHandle_t s_commands; // uint8_t (esp_presence_command_t)
static std::atomic<uint32_t> s_dropped{0};

// Inputs from the other tasks
static std::atomic<uint16_t> s_hold_time_s{0}; // 0: not read yet
static std::atomic<bool> s_busy{false};
static portMUX_TYPE s_range_lock = portMUX_INITIALIZER_UNLOCKED;
static pe_range_t s_range_in;  // s_range_lock
static bool s_range_pending;   // s_range_lock
static std::atomic<void (*)(uint8_t)> s_show_learn_mode{nullptr};

// Outputs to the other tasks
static std::atomic<bool> s_started{false};
static std::atomic<bool> s_occupied{false};
static std::atomic<bool> s_needs_engineering{false};
static std::atomic<uint32_t> s_silent_ms{0};

static uint32_t now_ms()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------------------------------------------------
// LD2410 reader task: never blocks

static void on_report(const ld2410_report_t *report)
{
    pe_frame_t f = {};
    f.engineering = report->engineering;
    if (report->engineering) {
        f.moving_gates = std::min<uint8_t>(report->moving_gates, PE_GATES);
        f.still_gates = std::min<uint8_t>(report->still_gates, PE_GATES);
        memcpy(f.moving, report->moving_energy, f.moving_gates);
        memcpy(f.still, report->still_energy, f.still_gates);
    }
    if (xQueueSend(s_frames, &f, 0) != pdTRUE) {
        s_dropped++;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Profile in NVS

static bool save_profile(const pe_profile_t *profile, void *ctx)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        // One blob: NVS keeps the old one until the new one is complete, so a power cut leaves either of them.
        err = nvs_set_blob(nvs, kNvsProfileKey, profile, sizeof(*profile));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save the learned thresholds in NVS (err:%d): the old ones stay", err);
        return false;
    }
    ESP_LOGI(TAG, "Learned thresholds #%" PRIu32 " saved in NVS", profile->sequence);
    return true;
}

static bool erase_profile()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, kNvsProfileKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK; // nothing stored
        } else if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not erase the learned thresholds in NVS (err:%d)", err);
        return false;
    }
    return true;
}

static void load_profile(uint32_t now)
{
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "No learned thresholds yet: built-in per-gate thresholds in use. Learn the empty room: "
                      "LEARN_EMPTY on the console, \"Learning\" in Home Assistant or 3 clicks");
        return;
    }
    pe_profile_t profile = {};
    size_t len = sizeof(profile);
    const esp_err_t err = nvs_get_blob(nvs, kNvsProfileKey, &profile, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No learned thresholds yet: built-in per-gate thresholds in use. Learn the empty room: "
                      "LEARN_EMPTY on the console, \"Learning\" in Home Assistant or 3 clicks");
        return;
    }
    if (len != sizeof(profile) || !pe_set_profile(&s_engine, &profile, now)) {
        // Left as it is in NVS: only a successful learning or PROFILE_RESET replaces it
        ESP_LOGE(TAG, "Stored thresholds damaged or of another version (%u bytes): built-in thresholds in use, learn "
                      "the empty room again", (unsigned)len);
        return;
    }
    ESP_LOGI(TAG, "Learned thresholds #%" PRIu32 " loaded (%u cm gates, %" PRIu32 " reports in %" PRIu32 " s)",
             profile.sequence, profile.gate_cm, profile.frames, profile.duration_s);
}

// ---------------------------------------------------------------------------------------------------------------------
// Logs

// "2.25-3 m"
static void format_gate(int gate, uint16_t gate_cm, char *buf, size_t len)
{
    if (gate_cm == 0) {
        snprintf(buf, len, "gate %d", gate);
        return;
    }
    char from[12];
    char to[12];
    radar_format_distance((uint32_t)gate * gate_cm, from, sizeof(from));
    radar_format_distance((uint32_t)(gate + 1) * gate_cm, to, sizeof(to));
    // "0 m-0.75 m" -> "0-0.75 m"
    char *unit = strstr(from, " m");
    if (unit) {
        *unit = '\0';
    }
    snprintf(buf, len, "%s-%s", from, to);
}

// "hold 15 trigger 21", "hold 60 trigger off", "off"
static void format_thresholds(uint8_t hold, uint8_t trigger, char *buf, size_t len)
{
    if (hold >= PE_OFF) {
        snprintf(buf, len, "not used");
    } else if (trigger >= PE_OFF) {
        snprintf(buf, len, "hold %u, cannot trigger", hold);
    } else {
        snprintf(buf, len, "hold %u trigger %u", hold, trigger);
    }
}

static void format_flags(uint8_t flags, char *buf, size_t len)
{
    snprintf(buf, len, "%s%s%s%s", flags & PE_CH_NO_DATA ? " no data" : "", flags & PE_CH_NOISY ? " TOO NOISY" : "",
             flags & PE_CH_HOLD_HIGH ? " hold high" : "", flags & PE_CH_DEFAULT ? " built-in" : "");
}

// Per gate: noise statistics and thresholds of a profile
static void log_profile_table(const pe_profile_t &p)
{
    for (int g = 0; g < PE_GATES; g++) {
        char where[24];
        format_gate(g, p.gate_cm, where, sizeof(where));
        char text[PE_KINDS][160];
        for (int kind = 0; kind < PE_KINDS; kind++) {
            const pe_channel_t &ch = p.ch[kind][g];
            char thresholds[40];
            char flags[48];
            format_thresholds(ch.hold, ch.trigger, thresholds, sizeof(thresholds));
            format_flags(ch.flags, flags, sizeof(flags));
            if (ch.flags & (PE_CH_NO_DATA | PE_CH_DEFAULT)) {
                snprintf(text[kind], sizeof(text[kind]), "%s%s", thresholds, flags);
            } else {
                snprintf(text[kind], sizeof(text[kind]), "noise %u+-%u.%u p99 %u max %u -> %s%s", ch.median,
                         ch.spread_x10 / 10, ch.spread_x10 % 10, ch.p99, ch.max, thresholds, flags);
            }
        }
        ESP_LOGI(TAG, "  gate %d %-11s moving: %s | still: %s", g, where, text[PE_MOVING], text[PE_STILL]);
    }
}

static void log_profile_in_use()
{
    const pe_engine_t &e = s_engine;
    if (pe_profile_learned(&e)) {
        ESP_LOGI(TAG, "Thresholds in use: learned #%" PRIu32 " (%u cm gates)", e.active->sequence, e.active->gate_cm);
    } else if (e.stored.learned) {
        ESP_LOGW(TAG, "Thresholds in use: built-in. The learned ones (#%" PRIu32 ") are for %u cm gates and the radar "
                      "now uses %u cm gates: learn the empty room again",
                 e.stored.sequence, e.stored.gate_cm, e.range.gate_cm);
    } else {
        ESP_LOGW(TAG, "Thresholds in use: built-in per-gate defaults. Learn the empty room (LEARN_EMPTY)");
    }
}

static void print_status(uint32_t now)
{
    const pe_engine_t &e = s_engine;
    char presence[96];
    if (e.presence == PE_PRESENCE_WAITING) {
        snprintf(presence, sizeof(presence), "empty, waiting for a strong signal");
    } else if (pe_sensor_fault(&e)) {
        snprintf(presence, sizeof(presence), "present, countdown paused (sensor fault)");
    } else if (e.evidence) {
        snprintf(presence, sizeof(presence), "present, hold signal at gate %u", e.evidence_gate);
    } else {
        const uint32_t left = pe_remaining_ms(&e, now);
        snprintf(presence, sizeof(presence), "present, empty in %" PRIu32 " s unless a hold signal comes back",
                 (left + 999) / 1000);
    }
    ESP_LOGI(TAG, "STATUS presence: %s; occupancy reported %s; hold time %" PRIu32 " s", presence,
             pe_occupied_report(&e) ? "yes" : "no", e.absence_ms / 1000);
    const uint32_t age = e.any_frame ? now - e.last_any_ms : now - e.start_ms;
    ESP_LOGI(TAG, "STATUS sensor: %s; last report %" PRIu32 " ms ago; reports: %" PRIu32 " with energies, %" PRIu32
                  " without, %" PRIu32 " damaged, %" PRIu32 " dropped",
             pe_sensor_name(e.sensor), age, e.frames_ok, e.frames_no_energy, e.frames_bad, s_dropped.load());
    if (e.learn_state == PE_LEARN_IDLE) {
        ESP_LOGI(TAG, "STATUS learning: idle; last: %s", pe_learn_result_name(e.learn_report.result));
    } else {
        ESP_LOGI(TAG, "STATUS learning: %s, %" PRIu32 " s left", pe_learn_state_name(e.learn_state),
                 (pe_learn_remaining_ms(&e, now) + 999) / 1000);
    }
    char moving[12];
    char still[12];
    radar_format_distance((uint32_t)(e.range.max_moving_gate + 1) * e.range.gate_cm, moving, sizeof(moving));
    radar_format_distance((uint32_t)(e.range.max_still_gate + 1) * e.range.gate_cm, still, sizeof(still));
    uint8_t raise = 0;
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            raise = std::max(raise, e.offset[kind][g]);
        }
    }
    ESP_LOGI(TAG, "STATUS range: moving gates 0-%u (to %s), still gates 0-%u (to %s); adaptation %s%s, highest raise "
                  "%u",
             e.range.max_moving_gate, e.range.gate_cm ? moving : "?", e.range.max_still_gate,
             e.range.gate_cm ? still : "?", e.p.adapt ? "on" : "off", e.quiet ? " (quiet)" : "", raise);
    log_profile_in_use();
}

static void print_diag(uint32_t now)
{
    const pe_engine_t &e = s_engine;
    const uint32_t left = pe_remaining_ms(&e, now);
    char countdown[32] = "";
    if (left != UINT32_MAX) {
        snprintf(countdown, sizeof(countdown), ", empty in %" PRIu32 " s", (left + 999) / 1000);
    }
    ESP_LOGI(TAG, "DIAG %s%s%s, sensor %s", e.presence == PE_PRESENCE_PRESENT ? "present" : "empty",
             e.presence == PE_PRESENCE_PRESENT && e.evidence ? " (hold signal)" : "", countdown,
             pe_sensor_name(e.sensor));
    ESP_LOGI(TAG, "DIAG gate distance    | moving now filt peak hold trig | still now filt peak hold trig");
    for (int g = 0; g < PE_GATES; g++) {
        char where[24];
        format_gate(g, e.range.gate_cm, where, sizeof(where));
        char cols[PE_KINDS][40];
        for (int kind = 0; kind < PE_KINDS; kind++) {
            if (!pe_channel_counted(&e, kind, g)) {
                snprintf(cols[kind], sizeof(cols[kind]), "%-28s", "  (not counted)");
                continue;
            }
            const uint8_t now_energy = kind == PE_MOVING ? e.last.moving[g] : e.last.still[g];
            char hold[6];
            char trig[6];
            const uint8_t h = pe_hold_threshold(&e, kind, g);
            const uint8_t t = pe_trigger_threshold(&e, kind, g);
            snprintf(hold, sizeof(hold), h >= PE_OFF ? "off" : "%u", h);
            snprintf(trig, sizeof(trig), t >= PE_OFF ? "off" : "%u", t);
            snprintf(cols[kind], sizeof(cols[kind]), "%10u %4u %4u %4s %4s", now_energy,
                     pe_filtered_energy(&e, kind, g), pe_recent_peak(&e, kind, g), hold, trig);
        }
        ESP_LOGI(TAG, "DIAG %4d %-10s |%s |%s", g, where, cols[PE_MOVING], cols[PE_STILL]);
    }
}

static void print_profile()
{
    const pe_engine_t &e = s_engine;
    if (e.stored.learned) {
        ESP_LOGI(TAG, "PROFILE learned #%" PRIu32 ": %u cm gates, %" PRIu32 " reports in %" PRIu32 " s",
                 e.stored.sequence, e.stored.gate_cm, e.stored.frames, e.stored.duration_s);
        log_profile_table(e.stored);
    } else {
        ESP_LOGI(TAG, "PROFILE built-in per-gate thresholds (no learning stored)");
        log_profile_table(e.defaults);
    }
    if (e.has_candidate && e.learn_report.result != PE_LEARN_OK) {
        ESP_LOGI(TAG, "PROFILE of the last learning, not used (%s):", pe_learn_result_name(e.learn_report.result));
        log_profile_table(e.candidate);
    }
}

static void log_learning_result()
{
    const pe_engine_t &e = s_engine;
    const pe_learn_report_t &r = e.learn_report;
    if (r.result == PE_LEARN_OK) {
        ESP_LOGI(TAG, "Learning: %s (%" PRIu32 " reports; %u gates counted, %u too noisy to trigger, %u with a high hold "
                      "threshold)",
                 pe_learn_result_name(r.result), r.frames, r.channels, r.noisy, r.hold_high);
    } else if (r.result == PE_LEARN_CANCELLED) {
        ESP_LOGI(TAG, "Learning cancelled: the old thresholds stay");
    } else {
        ESP_LOGE(TAG, "Learning %s (%" PRIu32 " reports, %" PRIu32 " without energies, longest silence %" PRIu32
                      " ms; %u gates counted, %u too noisy)",
                 pe_learn_result_name(r.result), r.frames, r.frames_no_energy, r.max_gap_ms, r.channels, r.noisy);
    }
    if (e.has_candidate && r.result != PE_LEARN_CANCELLED) {
        log_profile_table(e.candidate);
        if (r.noisy > 0 || r.hold_high > 0) {
            ESP_LOGW(TAG, "Gates marked TOO NOISY cannot start presence (something moves there in the empty room: fan, "
                          "curtain, plant, reflections); \"hold high\" gates may miss people sitting still. Move the "
                          "sensor, remove the source or reduce Range, then learn again.");
        }
    }
}

static void show_learn_mode(uint8_t mode)
{
    void (*show)(uint8_t) = s_show_learn_mode.load();
    if (show) {
        show(mode);
    }
}

static void handle_events(uint32_t now)
{
    pe_engine_t &e = s_engine;
    const uint32_t ev = pe_take_events(&e);
    if (ev & PE_EV_SENSOR) {
        if (e.sensor == PE_SENSOR_OK) {
            ESP_LOGI(TAG, "Radar energies received");
        } else if (e.sensor != PE_SENSOR_STARTING) {
            ESP_LOGW(TAG, "Sensor fault: %s. Presence stays as it is (%s) until data is back", pe_sensor_name(e.sensor),
                     e.presence == PE_PRESENCE_PRESENT ? "present" : "empty");
        }
    }
    if (ev & PE_EV_TRIGGERED) {
        ESP_LOGI(TAG, "Present: %s gate %u energy %u reached its trigger %u (%u of the last %u reports strong)",
                 e.trigger_kind == PE_MOVING ? "moving" : "still", e.trigger_gate, e.trigger_energy,
                 e.trigger_threshold, e.p.trigger_frames, e.p.trigger_window);
    }
    if (ev & PE_EV_EVIDENCE) {
        if (e.presence == PE_PRESENCE_PRESENT && !e.evidence) {
            ESP_LOGD(TAG, "No hold signal: empty in %" PRIu32 " s unless it comes back", e.absence_ms / 1000);
        } else if (e.evidence) {
            ESP_LOGD(TAG, "Hold signal at gate %u", e.evidence_gate);
        }
    }
    if (ev & PE_EV_ABSENT) {
        ESP_LOGI(TAG, "Empty: no hold signal for %" PRIu32 " s; waiting for a strong signal", e.absence_ms / 1000);
    }
    if (ev & PE_EV_LEARN_PHASE) {
        if (e.learn_state == PE_LEARN_WAITING) {
            ESP_LOGI(TAG, "LEARN_EMPTY: leave the room, recording the empty room in %" PRIu32 " s for %" PRIu32 " s",
                     e.p.learn_delay_ms / 1000, e.p.learn_duration_ms / 1000);
            status_led_set_learning(STATUS_LED_LEARNING_WAITING);
            show_learn_mode(RADAR_LEARN_ON);
        } else if (e.learn_state == PE_LEARN_RECORDING) {
            ESP_LOGI(TAG, "Recording the empty room for %" PRIu32 " s: stay out of the room", e.p.learn_duration_ms / 1000);
            status_led_set_learning(STATUS_LED_LEARNING_RECORDING);
        }
    }
    if (ev & PE_EV_LEARN_RESULT) {
        log_learning_result();
        const pe_learn_result_t result = e.learn_report.result;
        status_led_set_learning(result == PE_LEARN_OK          ? STATUS_LED_LEARNING_DONE
                                : result == PE_LEARN_CANCELLED ? STATUS_LED_LEARNING_NONE
                                                               : STATUS_LED_LEARNING_FAILED);
        show_learn_mode(RADAR_LEARN_OFF);
    }
    if (ev & PE_EV_PROFILE) {
        log_profile_in_use();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine task

static void run_command(uint8_t command, uint32_t now, bool *diag_stream)
{
    pe_engine_t &e = s_engine;
    switch (command) {
    case ESP_PRESENCE_LEARN:
        if (!pe_learn_start(&e, now)) {
            ESP_LOGW(TAG, "LEARN_EMPTY: a learning is running already (%s)", pe_learn_state_name(e.learn_state));
        } else if (pe_sensor_fault(&e)) {
            ESP_LOGW(TAG, "LEARN_EMPTY started while the sensor is faulty (%s): it fails unless data comes back",
                     pe_sensor_name(e.sensor));
        }
        break;
    case ESP_PRESENCE_LEARN_CANCEL:
        if (!pe_learn_cancel(&e, now)) {
            ESP_LOGI(TAG, "LEARN_CANCEL: no learning running");
            show_learn_mode(RADAR_LEARN_OFF);
        }
        break;
    case ESP_PRESENCE_PROFILE_RESET:
        (void)pe_learn_cancel(&e, now);
        show_learn_mode(RADAR_LEARN_FACTORY);
        if (erase_profile()) {
            pe_reset_profile(&e, now);
            ESP_LOGI(TAG, "PROFILE_RESET: learned thresholds erased, built-in per-gate thresholds in use");
            status_led_set_learning(STATUS_LED_LEARNING_FACTORY);
        } else {
            status_led_set_learning(STATUS_LED_LEARNING_FAILED);
        }
        show_learn_mode(RADAR_LEARN_OFF);
        break;
    case ESP_PRESENCE_PRINT_STATUS:
        print_status(now);
        break;
    case ESP_PRESENCE_PRINT_DIAG:
        print_diag(now);
        break;
    case ESP_PRESENCE_PRINT_PROFILE:
        print_profile();
        break;
    case ESP_PRESENCE_DIAG_STREAM_ON:
        *diag_stream = true;
        break;
    case ESP_PRESENCE_DIAG_STREAM_OFF:
        *diag_stream = false;
        break;
    default:
        break;
    }
}

static void apply_inputs(uint32_t now)
{
    pe_engine_t &e = s_engine;
    const uint16_t hold_s = s_hold_time_s.load();
    if (hold_s != 0 && (uint32_t)hold_s * 1000 != e.absence_ms) {
        pe_set_absence_time(&e, (uint32_t)hold_s * 1000);
        ESP_LOGI(TAG, "Hold time %u s: empty after %u s without a hold signal", hold_s, hold_s);
    }
    const bool busy = s_busy.load();
    if (busy != e.busy) {
        pe_set_busy(&e, busy, now);
    }
    pe_range_t range;
    taskENTER_CRITICAL(&s_range_lock);
    const bool pending = s_range_pending;
    range = s_range_in;
    s_range_pending = false;
    taskEXIT_CRITICAL(&s_range_lock);
    if (pending) {
        pe_set_range(&e, &range, now);
    }
}

static void publish(uint32_t now)
{
    const pe_engine_t &e = s_engine;
    s_occupied = pe_occupied_report(&e);
    const uint32_t silent = e.any_frame ? now - e.last_any_ms : now - e.start_ms;
    s_silent_ms = silent >= e.p.data_timeout_ms ? silent : 0;
    const bool fresh = e.any_frame && now - e.last_any_ms < kEngineeringCheckMs;
    const bool energies = e.energy_frame && now - e.last_energy_ms < kEngineeringCheckMs;
    s_needs_engineering = fresh && !energies;
}

static void engine_task(void *arg)
{
    uint32_t errors_seen = ld2410_frame_errors();
    bool diag_stream = false;
    uint32_t last_diag = now_ms();
    uint32_t last_status = last_diag;
    while (true) {
        pe_frame_t f;
        if (xQueueReceive(s_frames, &f, pdMS_TO_TICKS(kLoopMs)) == pdTRUE) {
            do {
                pe_step(&s_engine, &f, now_ms());
            } while (xQueueReceive(s_frames, &f, 0) == pdTRUE);
        }
        const uint32_t now = now_ms();
        apply_inputs(now);
        const uint32_t errors = ld2410_frame_errors();
        if (errors != errors_seen) {
            pe_count_bad_frames(&s_engine, errors - errors_seen);
            errors_seen = errors;
        }
        uint8_t command;
        while (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            run_command(command, now, &diag_stream);
        }
        pe_tick(&s_engine, now);
        handle_events(now);
        publish(now);

        if (diag_stream && now - last_diag >= kDiagStreamMs) {
            last_diag = now;
            print_diag(now);
        }
        if (CONFIG_PRESENCE_ENGINE_STATUS_LOG_SEC > 0 &&
            now - last_status >= (uint32_t)CONFIG_PRESENCE_ENGINE_STATUS_LOG_SEC * 1000) {
            last_status = now;
            print_status(now);
        }
    }
}

static void params_from_config(pe_params_t *p)
{
    pe_default_params(p);
    p->trigger_frames = CONFIG_PRESENCE_ENGINE_TRIGGER_REPORTS;
    p->trigger_window = CONFIG_PRESENCE_ENGINE_TRIGGER_WINDOW;
    p->hold_frames = CONFIG_PRESENCE_ENGINE_HOLD_REPORTS;
    p->hold_window = CONFIG_PRESENCE_ENGINE_HOLD_WINDOW;
    p->data_timeout_ms = CONFIG_PRESENCE_ENGINE_DATA_TIMEOUT_MS;
    p->learn_delay_ms = (uint32_t)CONFIG_LD2410_LEARN_DELAY_SEC * 1000;
    p->learn_duration_ms = (uint32_t)CONFIG_LD2410_LEARN_DURATION_SEC * 1000;
    p->k_hold_x10 = CONFIG_PRESENCE_ENGINE_HOLD_SPREADS_X10;
    p->k_trigger_x10 = CONFIG_PRESENCE_ENGINE_TRIGGER_SPREADS_X10;
    p->trigger_margin = CONFIG_PRESENCE_ENGINE_TRIGGER_MARGIN;
    p->max_noisy_percent = CONFIG_PRESENCE_ENGINE_MAX_NOISY_PERCENT;
#if CONFIG_PRESENCE_ENGINE_ADAPT
    p->adapt = true;
    p->adapt_max_up = CONFIG_PRESENCE_ENGINE_ADAPT_MAX_UP;
#else
    p->adapt = false;
#endif
}

esp_err_t esp_presence_start(void)
{
    s_frames = xQueueCreate(kFrameQueueLength, sizeof(pe_frame_t));
    s_commands = xQueueCreate(kCommandQueueLength, sizeof(uint8_t));
    if (!s_frames || !s_commands) {
        return ESP_ERR_NO_MEM;
    }
    pe_params_t params;
    params_from_config(&params);
    const uint32_t now = now_ms();
    pe_init(&s_engine, &params, save_profile, nullptr, now);
    pe_set_absence_time(&s_engine, (uint32_t)CONFIG_PRESENCE_HOLD_TIME_SEC * 1000);
    ESP_LOGI(TAG, "ESP32 presence engine: strong signal %u of %u reports to start, hold signal %u of %u reports to "
                  "stay, data timeout %" PRIu32 " ms, adaptation %s",
             s_engine.p.trigger_frames, s_engine.p.trigger_window, s_engine.p.hold_frames, s_engine.p.hold_window,
             s_engine.p.data_timeout_ms, s_engine.p.adapt ? "on" : "off");
    load_profile(now);
    if (xTaskCreate(engine_task, "presence_eng", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ld2410_set_report_hook(on_report);
    s_started = true;
    return ESP_OK;
}

bool esp_presence_command(esp_presence_command_t command)
{
    if (!s_started) {
        ESP_LOGW(TAG, "The presence engine does not run");
        return false;
    }
    const uint8_t value = (uint8_t)command;
    if (xQueueSend(s_commands, &value, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command ignored: the presence engine is busy");
        return false;
    }
    return true;
}

bool esp_presence_occupied(void)
{
    return s_occupied;
}

void esp_presence_set_hold_time(uint16_t seconds)
{
    s_hold_time_s = seconds;
}

void esp_presence_configure(const radar_settings_t *settings)
{
    pe_range_t range = {};
    range.gate_cm = settings->gate_cm;
    range.max_moving_gate = settings->config.max_moving_gate;
    range.max_still_gate = settings->config.max_still_gate;
    if (CONFIG_PRESENCE_MAX_DISTANCE_CM > 0 && settings->gate_cm > 0) {
        // Gates starting farther than the distance limit are not counted
        const uint8_t limit = (uint8_t)std::min<uint32_t>(CONFIG_PRESENCE_MAX_DISTANCE_CM / settings->gate_cm,
                                                          PE_GATES - 1);
        range.max_moving_gate = std::min(range.max_moving_gate, limit);
        range.max_still_gate = std::min(range.max_still_gate, limit);
    }
    taskENTER_CRITICAL(&s_range_lock);
    s_range_in = range;
    s_range_pending = true;
    taskEXIT_CRITICAL(&s_range_lock);
}

void esp_presence_set_radar_busy(bool busy)
{
    s_busy = busy;
}

bool esp_presence_needs_engineering(void)
{
    return s_needs_engineering;
}

uint32_t esp_presence_silent_ms(void)
{
    return s_silent_ms;
}

void esp_presence_set_learn_view(void (*show_learn_mode)(uint8_t mode))
{
    s_show_learn_mode = show_learn_mode;
}

#endif // CONFIG_PRESENCE_ESP_ENGINE
