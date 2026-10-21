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

#include "esp_presence.h"
#include "presence_engine.h"

static_assert(PE_GATES == LD2410_GATE_COUNT && PE_MAX_ENERGY == LD2410_MAX_ENERGY, "same gates and energies");

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
// New thresholds are logged once they stopped changing for this long (a learning gives the noise, then the radar's)
static constexpr uint32_t kThresholdLogDelayMs = 1500;

static constexpr const char *kNvsNamespace = "esp_presence";
static constexpr const char *kNvsNoiseKey = "noise";

// Engine task only, after esp_presence_start()
static pe_engine_t s_engine;
// Its parameters (as the engine checked them): set before the task starts, only read afterwards (radar_cfg task)
static pe_params_t s_params;
// Thresholds learned from the empty room (radar_cfg task)
static const pe_learn_params_t s_learn = {
    CONFIG_PRESENCE_ENGINE_LEARN_FLOOR_PERCENT, // floor_percent
    CONFIG_PRESENCE_ENGINE_LEARN_MIN_MARGIN,    // min_margin
    CONFIG_LD2410_LEARN_MAX_RAISE,              // max_raise
};
static QueueHandle_t s_frames;   // pe_frame_t, from the LD2410 reader task
static QueueHandle_t s_commands; // uint8_t (esp_presence_command_t)
static std::atomic<uint32_t> s_dropped{0};

// Inputs from the other tasks
static std::atomic<uint16_t> s_hold_time_s{0}; // 0: not read yet
static std::atomic<bool> s_busy{false};
static portMUX_TYPE s_input_lock = portMUX_INITIALIZER_UNLOCKED;
static pe_radar_thresholds_t s_radar_in; // s_input_lock
static pe_range_t s_range_in;            // s_input_lock
static bool s_config_pending;            // s_input_lock
static pe_noise_t s_noise_in;            // s_input_lock
static bool s_noise_pending;             // s_input_lock

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
// Noise in NVS

static bool save_noise(const pe_noise_t *noise)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        // One blob: NVS keeps the old one until the new one is complete, so a power cut leaves either of them.
        err = nvs_set_blob(nvs, kNvsNoiseKey, noise, sizeof(*noise));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save the noise of the empty room in NVS (err:%d): the old one stays", err);
        return false;
    }
    return true;
}

static void load_noise(uint32_t now)
{
    pe_noise_t noise = {};
    size_t len = sizeof(noise);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_blob(nvs, kNvsNoiseKey, &noise, &len);
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No noise of the empty room recorded yet: hold thresholds = radar thresholds, strong above "
                      "them without checking the noise. Learn the empty room (\"Learning\" in Home Assistant, 3 "
                      "clicks or LEARN_EMPTY)");
        return;
    }
    if (len != sizeof(noise) || !pe_set_noise(&s_engine, &noise, now)) {
        // Left as it is in NVS: only a successful learning replaces it
        ESP_LOGE(TAG, "Stored noise damaged or of another version (%u bytes): hold thresholds = radar thresholds, "
                      "learn the empty room again", (unsigned)len);
        return;
    }
    ESP_LOGI(TAG, "Noise of the empty room loaded (%" PRIu32 " reports, %u cm gates)", noise.frames, noise.gate_cm);
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

// "45", "never" (PE_OFF)
static void format_level(uint8_t level, char *buf, size_t len)
{
    if (level > PE_MAX_ENERGY) {
        snprintf(buf, len, "never");
    } else {
        snprintf(buf, len, "%u", level);
    }
}

// "radar 30 strong 31 hold 20 (noise 8)", "radar 40 strong 69 hold 66 (noise 64, strong and hold raised)",
// "radar 100 (gate ignored)"
static void format_channel(const pe_engine_t &e, int kind, int g, char *buf, size_t len)
{
    const pe_channel_t &ch = e.th.ch[kind][g];
    if (ch.flags & PE_CH_NOT_SET) {
        snprintf(buf, len, "no radar threshold");
        return;
    }
    if (ch.flags & PE_CH_IGNORED) {
        snprintf(buf, len, "radar %u (gate ignored)", ch.radar);
        return;
    }
    char strong[8];
    char hold[8];
    format_level(ch.trigger, strong, sizeof(strong));
    format_level(ch.hold, hold, sizeof(hold));
    if (ch.flags & PE_CH_NO_NOISE) {
        snprintf(buf, len, "radar %u strong %s hold %s", ch.radar, strong, hold);
        return;
    }
    const char *raised = (ch.flags & PE_CH_NOISE_TRIGGER) ? ", strong and hold raised"
                         : (ch.flags & PE_CH_NOISE_HOLD)  ? ", hold raised"
                                                          : "";
    snprintf(buf, len, "radar %u strong %s hold %s (noise %u%s)", ch.radar, strong, hold, e.noise.level[kind][g],
             raised);
}

static void log_thresholds()
{
    const pe_engine_t &e = s_engine;
    if (e.th.from_radar) {
        char margin[8] = "";
        if (e.p.trigger_margin > 0) {
            snprintf(margin, sizeof(margin), " + %u", e.p.trigger_margin);
        }
        ESP_LOGI(TAG, "Thresholds from the radar (%u cm gates): strong from radar + 1%s and at least the empty room "
                      "noise + %u; hold at radar - %u %% (at most %u), at least the noise + %u",
                 e.range.gate_cm, margin, e.p.trigger_noise_margin, e.p.hold_drop_percent, e.p.hold_drop_max,
                 e.p.hold_noise_margin);
    } else {
        ESP_LOGI(TAG, "Thresholds: LD2410 factory ones until the radar's are read");
    }
    for (int g = 0; g < PE_GATES; g++) {
        char where[24];
        format_gate(g, e.range.gate_cm, where, sizeof(where));
        char moving[80];
        char still[80];
        format_channel(e, PE_MOVING, g, moving, sizeof(moving));
        format_channel(e, PE_STILL, g, still, sizeof(still));
        ESP_LOGI(TAG, "  gate %d %-11s moving: %s | still: %s", g, where, moving, still);
    }
    if (e.th.from_radar && e.th.factory) {
        ESP_LOGW(TAG, "The radar holds its factory thresholds: learn the empty room (\"Learning\" in Home Assistant, "
                      "3 clicks or LEARN_EMPTY)");
    }
    if (!e.th.noise) {
        if (e.has_noise) {
            ESP_LOGW(TAG, "The noise was recorded with %u cm gates and the radar uses %u cm gates: hold = radar "
                          "threshold until the empty room is learned again", e.noise.gate_cm, e.range.gate_cm);
        } else {
            ESP_LOGW(TAG, "No noise of the empty room recorded: hold = radar threshold until the empty room is learned");
        }
        return;
    }
    pe_radar_thresholds_t factory;
    pe_factory_thresholds(&factory);
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            const pe_channel_t &ch = e.th.ch[kind][g];
            if (!pe_channel_counted(&e, kind, g) || !(ch.flags & PE_CH_NOISE_TRIGGER)) {
                continue;
            }
            const char *name = kind == PE_MOVING ? "moving" : "still";
            const uint8_t level = e.noise.level[kind][g];
            const int limit =
                (kind == PE_MOVING ? factory.moving[g] : factory.still[g]) + CONFIG_LD2410_LEARN_MAX_RAISE;
            if (ch.trigger > PE_MAX_ENERGY) {
                ESP_LOGW(TAG, "Gate %d %s: the empty room noise %u is close to 100, it cannot start presence there. If "
                              "something kept moving there while learning, learn the empty room again without it",
                         g, name, level);
            } else if (ch.radar >= limit) {
                ESP_LOGW(TAG, "Gate %d %s: the empty room noise %u reaches the radar threshold %u (a learning raises "
                              "it at most to factory + %d), strong there only from %u",
                         g, name, level, ch.radar, CONFIG_LD2410_LEARN_MAX_RAISE, ch.trigger);
            } else {
                ESP_LOGW(TAG, "Gate %d %s: the empty room noise %u reaches the radar threshold %u, strong there only "
                              "from %u. Learn the empty room again (the radar threshold is then raised above the "
                              "noise) or raise it with HLKRadarTool",
                         g, name, level, ch.radar, ch.trigger);
            }
        }
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
    char noise[64];
    if (e.th.noise) {
        snprintf(noise, sizeof(noise), "noise recorded (%" PRIu32 " reports)", e.noise.frames);
    } else {
        snprintf(noise, sizeof(noise), "%s", e.has_noise ? "noise of another gate length" : "no noise recorded");
    }
    ESP_LOGI(TAG, "STATUS thresholds: %s%s; %s", e.th.from_radar ? "from the radar" : "LD2410 factory, radar not read",
             e.th.from_radar && e.th.factory ? " (its factory thresholds: learn the empty room)" : "", noise);
    char moving[12];
    char still[12];
    radar_format_distance((uint32_t)(e.range.max_moving_gate + 1) * e.range.gate_cm, moving, sizeof(moving));
    radar_format_distance((uint32_t)(e.range.max_still_gate + 1) * e.range.gate_cm, still, sizeof(still));
    ESP_LOGI(TAG, "STATUS range: moving gates 0-%u (to %s), still gates %d-%u (to %s)", e.range.max_moving_gate,
             e.range.gate_cm ? moving : "?", PE_FIRST_STILL_GATE, e.range.max_still_gate,
             e.range.gate_cm ? still : "?");
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
    ESP_LOGI(TAG, "DIAG gate distance   | moving now filt peak hold strong radar | still now filt peak hold strong "
                  "radar");
    for (int g = 0; g < PE_GATES; g++) {
        char where[24];
        format_gate(g, e.range.gate_cm, where, sizeof(where));
        char cols[PE_KINDS][48];
        for (int kind = 0; kind < PE_KINDS; kind++) {
            // The first column ends under "now" of " moving now" or " still now"
            const int now_width = kind == PE_MOVING ? 11 : 10;
            if (!pe_channel_counted(&e, kind, g)) {
                snprintf(cols[kind], sizeof(cols[kind]), "%-*s", now_width + 28, "  (not counted)");
                continue;
            }
            const uint8_t now_energy = kind == PE_MOVING ? e.last.moving[g] : e.last.still[g];
            char strong[8];
            char hold[8];
            format_level(pe_trigger_threshold(&e, kind, g), strong, sizeof(strong));
            format_level(pe_hold_threshold(&e, kind, g), hold, sizeof(hold));
            snprintf(cols[kind], sizeof(cols[kind]), "%*u %4u %4u %4s %6s %5u", now_width, now_energy,
                     pe_filtered_energy(&e, kind, g), pe_recent_peak(&e, kind, g), hold, strong,
                     e.th.ch[kind][g].radar);
        }
        ESP_LOGI(TAG, "DIAG %4d %-10s |%s |%s", g, where, cols[PE_MOVING], cols[PE_STILL]);
    }
}

// True when the thresholds in use changed
static bool handle_events()
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
        if (e.trigger_fast) {
            ESP_LOGI(TAG, "Present (fast): moving gate %u energy %u, fast from %u (radar threshold %u), %u reports in "
                          "a row", e.trigger_gate, e.trigger_energy, e.trigger_strong, e.trigger_threshold,
                     e.p.fast_frames);
        } else {
            ESP_LOGI(TAG, "Present: %s gate %u energy %u, strong from %u (radar threshold %u), %u of the last %u "
                          "reports strong", e.trigger_kind == PE_MOVING ? "moving" : "still", e.trigger_gate,
                     e.trigger_energy, e.trigger_strong, e.trigger_threshold, e.p.trigger_frames, e.p.trigger_window);
        }
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
    return (ev & PE_EV_THRESHOLDS) != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine task

static void run_command(uint8_t command, uint32_t now, bool *diag_stream)
{
    switch (command) {
    case ESP_PRESENCE_PRINT_STATUS:
        print_status(now);
        break;
    case ESP_PRESENCE_PRINT_DIAG:
        print_diag(now);
        break;
    case ESP_PRESENCE_PRINT_THRESHOLDS:
        log_thresholds();
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
    pe_radar_thresholds_t radar;
    pe_range_t range;
    pe_noise_t noise;
    taskENTER_CRITICAL(&s_input_lock);
    const bool config = s_config_pending;
    const bool learned = s_noise_pending;
    radar = s_radar_in;
    range = s_range_in;
    noise = s_noise_in;
    s_config_pending = false;
    s_noise_pending = false;
    taskEXIT_CRITICAL(&s_input_lock);
    if (learned) {
        (void)pe_set_noise(&e, &noise, now); // valid: made by esp_presence_learned_room()
    }
    if (config) {
        pe_configure(&e, &radar, &range, now);
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
    bool thresholds_log = false;
    uint32_t thresholds_log_at = 0;
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
        if (handle_events()) {
            // Logged once they stopped changing
            thresholds_log = true;
            thresholds_log_at = now + kThresholdLogDelayMs;
        }
        if (thresholds_log && (int32_t)(now - thresholds_log_at) >= 0) {
            thresholds_log = false;
            log_thresholds();
        }
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
    p->fast_frames = CONFIG_PRESENCE_ENGINE_FAST_REPORTS;
    p->fast_margin = CONFIG_PRESENCE_ENGINE_FAST_MARGIN;
    p->hold_frames = CONFIG_PRESENCE_ENGINE_HOLD_REPORTS;
    p->hold_window = CONFIG_PRESENCE_ENGINE_HOLD_WINDOW;
    p->data_timeout_ms = CONFIG_PRESENCE_ENGINE_DATA_TIMEOUT_MS;
    p->hold_drop_percent = CONFIG_PRESENCE_ENGINE_HOLD_DROP_PERCENT;
    p->hold_drop_max = CONFIG_PRESENCE_ENGINE_HOLD_DROP_MAX;
    p->trigger_margin = CONFIG_PRESENCE_ENGINE_TRIGGER_MARGIN;
    p->trigger_noise_margin = CONFIG_PRESENCE_ENGINE_TRIGGER_NOISE_MARGIN;
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
    pe_init(&s_engine, &params, now);
    s_params = s_engine.p;
    pe_set_absence_time(&s_engine, (uint32_t)CONFIG_PRESENCE_HOLD_TIME_SEC * 1000);
    char margin[8] = "";
    if (s_params.trigger_margin > 0) {
        snprintf(margin, sizeof(margin), " + %u", s_params.trigger_margin);
    }
    ESP_LOGI(TAG, "ESP32 presence engine: strong signal (from radar threshold + 1%s, at least the empty room noise + "
                  "%u) in %u of %u reports to start, hold signal in %u of %u reports to stay, data timeout %" PRIu32
                  " ms", margin, s_params.trigger_noise_margin, s_params.trigger_frames, s_params.trigger_window,
             s_params.hold_frames, s_params.hold_window, s_params.data_timeout_ms);
    if (s_params.fast_frames > 0) {
        ESP_LOGI(TAG, "Fast start: moving energy at the strong level + %u in %u reports in a row", s_params.fast_margin,
                 s_params.fast_frames);
    }
    load_noise(now);
    (void)pe_take_events(&s_engine); // the thresholds are logged once read from the radar
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
    pe_radar_thresholds_t radar;
    memcpy(radar.moving, settings->config.thresholds.moving, sizeof(radar.moving));
    memcpy(radar.still, settings->config.thresholds.still, sizeof(radar.still));
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
    taskENTER_CRITICAL(&s_input_lock);
    s_radar_in = radar;
    s_range_in = range;
    s_config_pending = true;
    taskEXIT_CRITICAL(&s_input_lock);
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

// "avg 2 max 8 peak 9 -> 30 floor (was 40)", "not settable"
static void format_learned(const pe_room_t &room, int kind, int g, uint8_t was, uint8_t now, uint8_t flags, char *buf,
                           size_t len)
{
    if (kind == PE_STILL && g < PE_FIRST_STILL_GATE) {
        snprintf(buf, len, "not settable");
        return;
    }
    if (!room.valid[kind][g]) {
        snprintf(buf, len, "no reports, kept %u", now);
        return;
    }
    const char *tag = (flags & PE_LEARN_LIMIT)   ? " limit"
                      : (flags & PE_LEARN_FLOOR) ? " floor"
                      : (flags & PE_LEARN_NOISE) ? " noise"
                                                 : "";
    snprintf(buf, len, "avg %u max %u peak %u -> %u%s (was %u)", room.avg[kind][g], room.max[kind][g],
             room.peak[kind][g], now, tag, was);
}

static void log_learned(const pe_room_t &room, const pe_radar_thresholds_t &was, const pe_radar_thresholds_t &now,
                        const uint8_t flags[PE_KINDS][PE_GATES], uint16_t gate_cm)
{
    ESP_LOGI(TAG, "Empty room, %" PRIu32 " reports: threshold = max + the larger of %u and (max - avg), at least the "
                  "noise + %u, from %u %% of factory to factory + %u (max: highest median of 3 reports, peak: highest "
                  "report)", room.frames, s_learn.min_margin, s_params.trigger_noise_margin - 1, s_learn.floor_percent,
             s_learn.max_raise);
    for (int g = 0; g < PE_GATES; g++) {
        char where[24];
        format_gate(g, gate_cm, where, sizeof(where));
        char moving[64];
        char still[64];
        format_learned(room, PE_MOVING, g, was.moving[g], now.moving[g], flags[PE_MOVING][g], moving, sizeof(moving));
        format_learned(room, PE_STILL, g, was.still[g], now.still[g], flags[PE_STILL][g], still, sizeof(still));
        ESP_LOGI(TAG, "  gate %d %-11s moving: %s | still: %s", g, where, moving, still);
    }
    pe_radar_thresholds_t factory;
    pe_factory_thresholds(&factory);
    for (int kind = 0; kind < PE_KINDS; kind++) {
        for (int g = 0; g < PE_GATES; g++) {
            if (!(flags[kind][g] & PE_LEARN_LIMIT)) {
                continue;
            }
            ESP_LOGW(TAG, "Gate %d %s: max %u, average %u: threshold limited to %u (factory %u + %u). Something kept "
                          "moving there while recording (fan, curtain, someone)? Learn again without it",
                     g, kind == PE_MOVING ? "moving" : "still", room.max[kind][g], room.avg[kind][g],
                     kind == PE_MOVING ? now.moving[g] : now.still[g],
                     kind == PE_MOVING ? factory.moving[g] : factory.still[g], s_learn.max_raise);
        }
    }
}

void esp_presence_learned_room(const ld2410_capture_t *capture, uint16_t gate_cm, ld2410_thresholds_t *thresholds)
{
    pe_noise_t noise;
    pe_noise_from_histograms(capture->moving, capture->still, capture->frames, gate_cm, &noise);
    if (save_noise(&noise)) {
        ESP_LOGI(TAG, "Noise of the empty room recorded (%" PRIu32 " reports) and saved: the thresholds stay above "
                      "it", noise.frames);
        taskENTER_CRITICAL(&s_input_lock);
        s_noise_in = noise;
        s_noise_pending = true;
        taskEXIT_CRITICAL(&s_input_lock);
    }
    pe_room_t room;
    pe_room_from_histograms(capture->moving, capture->still, capture->moving_filtered_max,
                            capture->still_filtered_max, capture->frames, &room);
    pe_radar_thresholds_t was;
    memcpy(was.moving, thresholds->moving, sizeof(was.moving));
    memcpy(was.still, thresholds->still, sizeof(was.still));
    pe_radar_thresholds_t now;
    uint8_t flags[PE_KINDS][PE_GATES];
    pe_learn_thresholds(&s_params, &s_learn, &room, &noise, &was, &now, flags);
    log_learned(room, was, now, flags, gate_cm);
    memcpy(thresholds->moving, now.moving, sizeof(now.moving));
    memcpy(thresholds->still, now.still, sizeof(now.still));
}

#endif // CONFIG_PRESENCE_ESP_ENGINE
