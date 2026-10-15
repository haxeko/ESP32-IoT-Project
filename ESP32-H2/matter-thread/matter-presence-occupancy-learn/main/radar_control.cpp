/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// A ChangeToMode command from Home Assistant reaches radar_control_pre_update()
// on the Matter task, which only records the request and wakes the worker:
// radar commands wait for ACKs and must not block the Matter task. The worker
// then reports CurrentMode from what the radar actually holds.

#include <app/clusters/mode-select-server/SupportedModesManager.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>

#include <drivers/ld2410.h>
#include <quiet_learn.h>
#include <radar_control.h>
#include <radar_settings.h>

static const char *TAG = "radar_ctl";

using namespace esp_matter;
using namespace chip::app::Clusters;
using ModeOption = ModeSelect::Structs::ModeOptionStruct::Type;
using chip::Protocols::InteractionModel::Status;

static constexpr uint32_t kTaskStack = 4096;
// Above the Matter task (1), below OpenThread (5), like the presence task
static constexpr UBaseType_t kTaskPriority = 2;
// Next reads while the radar does not answer (not connected, still starting)
static constexpr uint32_t kSyncRetryFirstMs = 15 * 1000;
static constexpr uint32_t kSyncRetryMaxMs = 5 * 60 * 1000;

static constexpr uint8_t kLearnOff = 0;
static constexpr uint8_t kLearnOn = 1;

static constexpr size_t kRangeOptionCount = LD2410_MAX_MAX_GATE - LD2410_MIN_MAX_GATE + 1;
// LD2410 factory no-one duration, shown until the radar has been read
static constexpr uint16_t kFactoryDurationS = 5;

static constexpr size_t kDistanceLabelLen = 12; // "5.25 m"
static constexpr size_t kCustomLabelLen = 48;   // "Moving 5.25 m, still 5.25 m"

// ---------------------------------------------------------------------------
// Mode options. The worker changes them with the Matter stack lock held (or
// before esp_matter::start()); the Matter task reads them to serve
// SupportedModes and to check ChangeToMode.

struct ModeList {
    chip::EndpointId endpoint_id;
    ModeOption *options;
    size_t preset_count; // always listed, followed by the RADAR_MODE_CUSTOM option when shown
    size_t count;
    char *custom_label;
};

static ModeOption s_learn_options[2];
static ModeOption s_duration_options[RADAR_DURATION_OPTION_COUNT + 1];
static ModeOption s_range_options[kRangeOptionCount + 1];
static char s_range_labels[kRangeOptionCount][kDistanceLabelLen];
static char s_duration_custom_label[kCustomLabelLen];
static char s_range_custom_label[kCustomLabelLen];

static ModeList s_learn = { chip::kInvalidEndpointId, s_learn_options, 2, 2, nullptr };
static ModeList s_duration = { chip::kInvalidEndpointId, s_duration_options, RADAR_DURATION_OPTION_COUNT,
                               RADAR_DURATION_OPTION_COUNT, s_duration_custom_label };
static ModeList s_range = { chip::kInvalidEndpointId, s_range_options, kRangeOptionCount, kRangeOptionCount,
                            s_range_custom_label };

static void set_option(ModeOption &option, const char *label, uint8_t mode)
{
    option.label = chip::CharSpan::fromCharString(label);
    option.mode = mode;
}

// Shows label as the RADAR_MODE_CUSTOM option, or removes that option (nullptr).
// Returns true when the list changed.
static bool set_custom_option(ModeList &list, const char *label)
{
    const bool shown = list.count > list.preset_count;
    if (label == nullptr) {
        list.count = list.preset_count;
        return shown;
    }
    if (shown && strcmp(list.custom_label, label) == 0) {
        return false;
    }
    strlcpy(list.custom_label, label, kCustomLabelLen);
    set_option(list.options[list.preset_count], list.custom_label, RADAR_MODE_CUSTOM);
    list.count = list.preset_count + 1;
    return true;
}

// Range option labels for a gate length. Returns true when they changed.
static bool set_range_labels(uint16_t gate_cm)
{
    bool changed = false;
    for (size_t i = 0; i < kRangeOptionCount; i++) {
        const uint8_t gate = LD2410_MIN_MAX_GATE + i;
        char label[kDistanceLabelLen];
        radar_format_distance((uint32_t)gate * gate_cm, label, sizeof(label));
        if (strcmp(label, s_range_labels[i]) != 0) {
            strlcpy(s_range_labels[i], label, sizeof(s_range_labels[i]));
            changed = true;
        }
        set_option(s_range_options[i], s_range_labels[i], gate);
    }
    return changed;
}

// One manager serves every Mode Select cluster of the device: the Mode Select
// server keeps a single global one.
class RadarModesManager : public ModeSelect::SupportedModesManager
{
public:
    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId endpointId) const override
    {
        const ModeList *list = find(endpointId);
        if (list == nullptr) {
            return ModeOptionsProvider();
        }
        return ModeOptionsProvider(list->options, list->options + list->count);
    }

    Status getModeOptionByMode(chip::EndpointId endpointId, uint8_t mode, const ModeOption **dataPtr) const override
    {
        const ModeList *list = find(endpointId);
        if (list != nullptr) {
            for (size_t i = 0; i < list->count; i++) {
                if (list->options[i].mode == mode) {
                    *dataPtr = &list->options[i];
                    return Status::Success;
                }
            }
        }
        return Status::InvalidCommand;
    }

private:
    static const ModeList *find(chip::EndpointId endpointId)
    {
        for (const ModeList *list : { &s_learn, &s_duration, &s_range }) {
            if (list->endpoint_id == endpointId) {
                return list;
            }
        }
        return nullptr;
    }
};

static RadarModesManager s_modes;

// ---------------------------------------------------------------------------
// Requests from the Matter task to the worker

// Empty room learning: requested by Home Assistant, run by the worker.
enum LearnState : uint8_t { kLearnIdle, kLearnRequested, kLearnRunning, kLearnCancelling };
static std::atomic<LearnState> s_learn_state{kLearnIdle};

// Duration (seconds) and Range (gate) chosen in Home Assistant, RADAR_KEEP once taken.
static std::atomic<int32_t> s_want_duration_s{RADAR_KEEP};
static std::atomic<int32_t> s_want_gate{RADAR_KEEP};

// Set by the worker once the radar settings have been read.
static std::atomic<bool> s_synced{false};
static std::atomic<TaskHandle_t> s_task{nullptr};

// Matter task: Learning starts a learning (or keeps a cancelled one running),
// Off cancels it.
static void request_learning(bool on)
{
    LearnState state = s_learn_state.load();
    LearnState next;
    do {
        if (on) {
            next = state == kLearnIdle ? kLearnRequested : state == kLearnCancelling ? kLearnRunning : state;
        } else {
            next = state == kLearnRequested ? kLearnIdle : state == kLearnRunning ? kLearnCancelling : state;
        }
    } while (next != state && !s_learn_state.compare_exchange_weak(state, next));
}

// ---------------------------------------------------------------------------
// Worker task

static ld2410_config_t s_config; // last settings read from the radar (s_synced)
static uint16_t s_gate_cm;       // gate length, 0 until asked to the radar

static void report_current_mode(chip::EndpointId endpoint_id, uint8_t mode)
{
    esp_matter_attr_val_t val = esp_matter_uint8(mode);
    esp_err_t err = attribute::report(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to report the mode of endpoint %u, err:%d", endpoint_id, err);
    }
}

// Matter stack lock held
static void report_supported_modes(chip::EndpointId endpoint_id)
{
    data_model::provider::get_instance().ReportAttributeChanged(
        chip::app::AttributePathParams(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::SupportedModes::Id));
}

static bool is_range_option(const ld2410_config_t &cfg)
{
    return cfg.max_moving_gate == cfg.max_still_gate && cfg.max_moving_gate >= LD2410_MIN_MAX_GATE &&
           cfg.max_moving_gate <= LD2410_MAX_MAX_GATE;
}

// Shows the radar settings in the Duration and Range selects.
static void publish(const ld2410_config_t &cfg)
{
    const int duration_option = radar_duration_option(cfg.no_one_duration_s);
    char duration_label[kCustomLabelLen] = "";
    if (duration_option < 0) {
        radar_format_duration(cfg.no_one_duration_s, duration_label, sizeof(duration_label));
    }
    const bool range_option = is_range_option(cfg);
    char range_label[kCustomLabelLen] = "";
    if (!range_option) {
        char moving[kDistanceLabelLen];
        char still[kDistanceLabelLen];
        radar_format_distance((uint32_t)cfg.max_moving_gate * s_gate_cm, moving, sizeof(moving));
        radar_format_distance((uint32_t)cfg.max_still_gate * s_gate_cm, still, sizeof(still));
        snprintf(range_label, sizeof(range_label), "Moving %s, still %s", moving, still);
    }

    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        if (set_custom_option(s_duration, duration_option < 0 ? duration_label : nullptr)) {
            report_supported_modes(s_duration.endpoint_id);
        }
        const bool labels_changed = set_range_labels(s_gate_cm);
        if (set_custom_option(s_range, range_option ? nullptr : range_label) || labels_changed) {
            report_supported_modes(s_range.endpoint_id);
        }
    }
    // report() takes the stack lock itself
    report_current_mode(s_duration.endpoint_id, duration_option >= 0 ? duration_option : RADAR_MODE_CUSTOM);
    report_current_mode(s_range.endpoint_id, range_option ? cfg.max_moving_gate : RADAR_MODE_CUSTOM);
}

static void log_config(const char *what, const ld2410_config_t &cfg)
{
    char duration[kCustomLabelLen];
    char moving[kDistanceLabelLen];
    char still[kDistanceLabelLen];
    radar_format_duration(cfg.no_one_duration_s, duration, sizeof(duration));
    radar_format_distance((uint32_t)cfg.max_moving_gate * s_gate_cm, moving, sizeof(moving));
    radar_format_distance((uint32_t)cfg.max_still_gate * s_gate_cm, still, sizeof(still));
    ESP_LOGI(TAG, "%s: no-one duration %s, range moving %s (gate %u), still %s (gate %u), %u cm per gate", what,
             duration, moving, cfg.max_moving_gate, still, cfg.max_still_gate, s_gate_cm);
}

// Reads the radar settings, applies the requested ones and shows the result
// in Home Assistant.
static void settings_session(int32_t want_duration_s, int32_t want_gate)
{
    const bool change = want_duration_s != RADAR_KEEP || want_gate != RADAR_KEEP;
    ld2410_config_t cfg;
    const esp_err_t err = radar_settings_session(want_duration_s, want_gate, &s_gate_cm, &cfg);
    if (err != ESP_OK) {
        if (change && s_synced) {
            ESP_LOGE(TAG, "Changing the radar settings failed, err:%d", err);
            publish(s_config); // back to the settings last read from the radar
        }
        return;
    }
    if (!s_synced || change) {
        log_config(s_synced ? "New LD2410 settings" : "LD2410 settings", cfg);
    }
    s_config = cfg;
    s_synced = true;
    publish(cfg);
}

static bool learning_cancelled()
{
    return s_learn_state.load() == kLearnCancelling;
}

static void run_requested_learning()
{
    LearnState state = kLearnRequested;
    if (!s_learn_state.compare_exchange_strong(state, kLearnRunning)) {
        return;
    }
    // Normally shown already; covers a request made while the previous learning ended.
    report_current_mode(s_learn.endpoint_id, kLearnOn);
    quiet_learn_run(learning_cancelled);
    s_learn_state = kLearnIdle;
    report_current_mode(s_learn.endpoint_id, kLearnOff);
}

static void radar_task(void *arg)
{
    uint32_t retry_ms = kSyncRetryFirstMs;
    while (true) {
        // Settings first: a session takes well under a second, a learning minutes.
        const int32_t want_duration_s = s_want_duration_s.exchange(RADAR_KEEP);
        const int32_t want_gate = s_want_gate.exchange(RADAR_KEEP);
        if (!s_synced || want_duration_s != RADAR_KEEP || want_gate != RADAR_KEEP) {
            settings_session(want_duration_s, want_gate);
        }
        run_requested_learning();

        TickType_t wait = portMAX_DELAY;
        if (!s_synced) {
            ESP_LOGW(TAG, "Could not read the LD2410 settings (check the wire from GPIO%d to the radar RX pin), "
                          "next try in %" PRIu32 " s", CONFIG_LD2410_UART_TX_PIN, retry_ms / 1000);
            wait = pdMS_TO_TICKS(retry_ms);
            retry_ms = std::min(retry_ms * 2, kSyncRetryMaxMs);
        }
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

// ---------------------------------------------------------------------------

static cluster_t *create_mode_select(endpoint_t *endpoint, const char *description, uint8_t initial_mode)
{
    cluster::mode_select::config_t config;
    strlcpy(config.description, description, sizeof(config.description));
    config.current_mode = initial_mode;
    config.delegate = &s_modes;
    return cluster::mode_select::create(endpoint, &config, CLUSTER_FLAG_SERVER);
}

// Mode Select device type (0x0027) endpoint
static endpoint_t *create_mode_select_endpoint(node_t *node, const char *description, uint8_t initial_mode)
{
    endpoint::mode_select::config_t config;
    strlcpy(config.mode_select.description, description, sizeof(config.mode_select.description));
    config.mode_select.current_mode = initial_mode;
    config.mode_select.delegate = &s_modes;
    endpoint_t *ep = endpoint::mode_select::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    VerifyOrReturnValue(ep != nullptr && cluster::get(ep, ModeSelect::Id) != nullptr, nullptr);
    return ep;
}

esp_err_t radar_control_create(node_t *node, endpoint_t *occupancy_endpoint)
{
    set_option(s_learn_options[0], "Off", kLearnOff);
    set_option(s_learn_options[1], "Learning", kLearnOn);
    for (size_t i = 0; i < RADAR_DURATION_OPTION_COUNT; i++) {
        set_option(s_duration_options[i], radar_duration_options[i].label, (uint8_t)i);
    }
    set_range_labels(LD2410_DEFAULT_GATE_CM);

    // CurrentMode is persisted, so a learning cut by a power loss would come
    // back as Learning: start from Off.
    cluster_t *learn = create_mode_select(occupancy_endpoint, "Learn empty room", kLearnOff);
    VerifyOrReturnError(learn != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the learning Mode Select"));
    attribute_t *current_mode = attribute::get(learn, ModeSelect::Attributes::CurrentMode::Id);
    VerifyOrReturnError(current_mode != nullptr, ESP_FAIL);
    esp_matter_attr_val_t off = esp_matter_uint8(kLearnOff);
    esp_err_t err = attribute::set_val(current_mode, &off, false);
    VerifyOrReturnError(err == ESP_OK || err == ESP_ERR_NOT_FINISHED, err); // NOT_FINISHED: already Off
    s_learn.endpoint_id = endpoint::get_id(occupancy_endpoint);

    // Until the worker has read the radar, Duration and Range show the last
    // persisted mode, or the LD2410 factory settings.
    endpoint_t *ep = create_mode_select_endpoint(node, "Duration", (uint8_t)radar_duration_option(kFactoryDurationS));
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Duration endpoint"));
    s_duration.endpoint_id = endpoint::get_id(ep);

    ep = create_mode_select_endpoint(node, "Range", LD2410_MAX_MAX_GATE);
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Range endpoint"));
    s_range.endpoint_id = endpoint::get_id(ep);

    ESP_LOGI(TAG, "Learn empty room on endpoint %u, Duration on endpoint %u, Range on endpoint %u",
             s_learn.endpoint_id, s_duration.endpoint_id, s_range.endpoint_id);
    return ESP_OK;
}

esp_err_t radar_control_start(void)
{
    TaskHandle_t task = nullptr;
    if (xTaskCreate(radar_task, "radar_cfg", kTaskStack, nullptr, kTaskPriority, &task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_task = task;
    return ESP_OK;
}

esp_err_t radar_control_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                   const esp_matter_attr_val_t *val)
{
    if (cluster_id != ModeSelect::Id || attribute_id != ModeSelect::Attributes::CurrentMode::Id) {
        return ESP_OK;
    }
    const uint8_t mode = val->val.u8;
    TaskHandle_t task = s_task;

    if (endpoint_id == s_learn.endpoint_id) {
        if (mode == kLearnOn && task == nullptr) {
            ESP_LOGE(TAG, "Cannot start the empty room learning: the radar task is not running");
            return ESP_ERR_INVALID_STATE;
        }
        request_learning(mode == kLearnOn);
    } else if (endpoint_id == s_duration.endpoint_id || endpoint_id == s_range.endpoint_id) {
        if (mode == RADAR_MODE_CUSTOM) {
            return ESP_OK; // the radar's own value: nothing to change
        }
        if (task == nullptr || !s_synced) {
            // Rejected, so Home Assistant keeps showing the current value.
            ESP_LOGW(TAG, "The LD2410 settings have not been read yet, change ignored");
            return ESP_ERR_INVALID_STATE;
        }
        if (endpoint_id == s_duration.endpoint_id) {
            VerifyOrReturnError(mode < RADAR_DURATION_OPTION_COUNT, ESP_ERR_INVALID_ARG);
            s_want_duration_s = radar_duration_options[mode].seconds;
        } else {
            VerifyOrReturnError(mode >= LD2410_MIN_MAX_GATE && mode <= LD2410_MAX_MAX_GATE, ESP_ERR_INVALID_ARG);
            s_want_gate = mode;
        }
    } else {
        return ESP_OK;
    }
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
    return ESP_OK;
}
