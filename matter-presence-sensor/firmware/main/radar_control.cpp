/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// A ChangeToMode command from Home Assistant reaches radar_control_pre_update() on the Matter task, which only
// records the request and wakes the radar_cfg task: radar commands wait for ACKs and must not block the Matter
// task. The radar_cfg task then reports CurrentMode from what the radar actually holds (show_settings()).

#include <app/clusters/mode-select-server/SupportedModesManager.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_log.h>
#include <string.h>

#include <drivers/ld2410.h>

#include "radar_control.h"
#include "radar_worker.h"

static const char *TAG = "radar_ctl";

using namespace esp_matter;
using namespace chip::app::Clusters;
using ModeOption = ModeSelect::Structs::ModeOptionStruct::Type;
using chip::Protocols::InteractionModel::Status;

static constexpr size_t kRangeOptionCount = LD2410_MAX_MAX_GATE - LD2410_MIN_MAX_GATE + 1;
static constexpr size_t kDistanceLabelLen = 12; // "5.25 m"
static constexpr size_t kCustomLabelLen = 48;   // "Moving 5.25 m, still 5.25 m"

// Root, occupancy sensor, Range, Radar resolution and Radar Bluetooth
static_assert(CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT >= 5,
              "CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT must be at least 5 (see sdkconfig.defaults)");

// ---------------------------------------------------------------------------------------------------------------------
// Mode options. The radar_cfg task changes them with the Matter stack lock held (or before esp_matter::start());
// the Matter task reads them to serve SupportedModes and to check ChangeToMode.

struct ModeList {
    chip::EndpointId endpoint_id;
    ModeOption *options;
    size_t preset_count; // always listed, followed by the RADAR_MODE_CUSTOM option when shown
    size_t count;
};

static ModeOption s_learn_options[3];
static ModeOption s_resolution_options[2];
static ModeOption s_range_options[kRangeOptionCount + 1];
static char s_range_labels[kRangeOptionCount][kDistanceLabelLen];
static char s_range_custom_label[kCustomLabelLen];
// Off, On, then "Not available" (RADAR_MODE_CUSTOM) listed alone
static ModeOption s_bluetooth_options[3];

static ModeList s_learn = { chip::kInvalidEndpointId, s_learn_options, 3, 3 };
static ModeList s_resolution = { chip::kInvalidEndpointId, s_resolution_options, 2, 2 };
static ModeList s_range = { chip::kInvalidEndpointId, s_range_options, kRangeOptionCount, kRangeOptionCount };
static ModeList s_bluetooth = { chip::kInvalidEndpointId, s_bluetooth_options, 2, 2 };

static void set_option(ModeOption &option, const char *label, uint8_t mode)
{
    option.label = chip::CharSpan::fromCharString(label);
    option.mode = mode;
}

// Shows label as the RADAR_MODE_CUSTOM Range option, or removes that option (nullptr). True when the list changed.
static bool set_range_custom_option(const char *label)
{
    const bool shown = s_range.count > s_range.preset_count;
    if (label == nullptr) {
        s_range.count = s_range.preset_count;
        return shown;
    }
    if (shown && strcmp(s_range_custom_label, label) == 0) {
        return false;
    }
    strlcpy(s_range_custom_label, label, sizeof(s_range_custom_label));
    set_option(s_range_options[s_range.preset_count], s_range_custom_label, RADAR_MODE_CUSTOM);
    s_range.count = s_range.preset_count + 1;
    return true;
}

// Range option labels for a gate length ("1.5 m" ... "6 m", or "0.4 m" ... "1.6 m"). True when they changed.
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

// 0.2 m is only listed when the radar can change its resolution. True when the list changed.
static bool set_resolution_options(bool resolution_settable)
{
    const size_t count = resolution_settable ? 2 : 1;
    const bool changed = s_resolution.count != count;
    s_resolution.count = count;
    return changed;
}

// Off and On, or only "Not available" when the radar does not answer the Bluetooth query (LD2410 without
// Bluetooth). True when the list changed.
static bool set_bluetooth_options(bool bluetooth_known)
{
    ModeOption *options = bluetooth_known ? &s_bluetooth_options[0] : &s_bluetooth_options[2];
    const size_t count = bluetooth_known ? 2 : 1;
    const bool changed = s_bluetooth.options != options || s_bluetooth.count != count;
    s_bluetooth.options = options;
    s_bluetooth.count = count;
    return changed;
}

// One manager serves every Mode Select cluster of the device: the Mode Select server keeps a single global one.
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
        for (const ModeList *list : { &s_learn, &s_range, &s_resolution, &s_bluetooth }) {
            if (list->endpoint_id == endpointId) {
                return list;
            }
        }
        return nullptr;
    }
};

static RadarModesManager s_modes;

// ---------------------------------------------------------------------------------------------------------------------
// What the radar_cfg task shows (radar_worker_view_t), on that task

// Takes the Matter stack lock itself
static void report_current_mode(chip::EndpointId endpoint_id, uint8_t mode)
{
    if (endpoint_id == chip::kInvalidEndpointId) {
        return;
    }
    esp_matter_attr_val_t val = esp_matter_uint8(mode);
    const esp_err_t err = attribute::report(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to report the mode of endpoint %u, err:%d", endpoint_id, err);
    }
}

// Matter stack lock held
static void report_supported_modes(chip::EndpointId endpoint_id)
{
    if (endpoint_id == chip::kInvalidEndpointId) {
        return;
    }
    data_model::provider::get_instance().ReportAttributeChanged(
        chip::app::AttributePathParams(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::SupportedModes::Id));
}

static bool is_range_option(const ld2410_config_t &cfg)
{
    return cfg.max_moving_gate == cfg.max_still_gate && cfg.max_moving_gate >= LD2410_MIN_MAX_GATE &&
           cfg.max_moving_gate <= LD2410_MAX_MAX_GATE;
}

// Range, Radar resolution and Radar Bluetooth show the settings the radar holds.
static void show_settings(const radar_settings_t *settings)
{
    const ld2410_config_t &cfg = settings->config;
    const bool range_option = is_range_option(cfg);
    char range_label[kCustomLabelLen] = "";
    if (!range_option) {
        radar_format_range(cfg.max_moving_gate, cfg.max_still_gate, settings->gate_cm, range_label,
                           sizeof(range_label));
    }
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        const bool labels_changed = set_range_labels(settings->gate_cm);
        if (set_range_custom_option(range_option ? nullptr : range_label) || labels_changed) {
            report_supported_modes(s_range.endpoint_id);
        }
        if (set_resolution_options(settings->resolution_settable)) {
            report_supported_modes(s_resolution.endpoint_id);
        }
        if (set_bluetooth_options(settings->bluetooth_known)) {
            report_supported_modes(s_bluetooth.endpoint_id);
        }
    }
    report_current_mode(s_range.endpoint_id, range_option ? cfg.max_moving_gate : RADAR_MODE_CUSTOM);
    report_current_mode(s_resolution.endpoint_id,
                        settings->gate_cm == LD2410_FINE_GATE_CM ? RADAR_RESOLUTION_FINE : RADAR_RESOLUTION_COARSE);
    report_current_mode(s_bluetooth.endpoint_id, !settings->bluetooth_known ? RADAR_MODE_CUSTOM
                                                 : settings->bluetooth_on   ? RADAR_BLUETOOTH_ON
                                                                            : RADAR_BLUETOOTH_OFF);
}

static void show_learn_mode(uint8_t mode)
{
    report_current_mode(s_learn.endpoint_id, mode);
}

// ---------------------------------------------------------------------------------------------------------------------

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
    set_option(s_learn_options[0], "Off", RADAR_LEARN_OFF);
    set_option(s_learn_options[1], "Learning", RADAR_LEARN_ON);
    set_option(s_learn_options[2], "Factory thresholds", RADAR_LEARN_FACTORY);
    set_range_labels(LD2410_DEFAULT_GATE_CM);
    set_option(s_resolution_options[0], "0.75 m", RADAR_RESOLUTION_COARSE);
    set_option(s_resolution_options[1], "0.2 m", RADAR_RESOLUTION_FINE);
    set_option(s_bluetooth_options[0], "Off", RADAR_BLUETOOTH_OFF);
    set_option(s_bluetooth_options[1], "On", RADAR_BLUETOOTH_ON);
    set_option(s_bluetooth_options[2], "Not available", RADAR_MODE_CUSTOM);

    // CurrentMode is persisted, so a learning cut by a power loss would come back as Learning: start from Off.
    cluster_t *learn = create_mode_select(occupancy_endpoint, "Learn empty room", RADAR_LEARN_OFF);
    VerifyOrReturnError(learn != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the learning Mode Select"));
    attribute_t *current_mode = attribute::get(learn, ModeSelect::Attributes::CurrentMode::Id);
    VerifyOrReturnError(current_mode != nullptr, ESP_FAIL);
    esp_matter_attr_val_t off = esp_matter_uint8(RADAR_LEARN_OFF);
    const esp_err_t err = attribute::set_val(current_mode, &off, false);
    VerifyOrReturnError(err == ESP_OK || err == ESP_ERR_NOT_FINISHED, err); // NOT_FINISHED: already Off
    s_learn.endpoint_id = endpoint::get_id(occupancy_endpoint);

    // Endpoints 2, 3, 4 in this order: the IDs follow the creation order and must stay the same for the
    // controllers that paired with an earlier firmware. Until the radar settings are read, Range, Radar resolution
    // and Radar Bluetooth show the last persisted mode, or the LD2410 factory settings (Bluetooth: off, the
    // default the radar_cfg task applies).
    endpoint_t *ep = create_mode_select_endpoint(node, "Range", LD2410_MAX_MAX_GATE);
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Range endpoint"));
    s_range.endpoint_id = endpoint::get_id(ep);

    ep = create_mode_select_endpoint(node, "Radar resolution", RADAR_RESOLUTION_COARSE);
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Radar resolution endpoint"));
    s_resolution.endpoint_id = endpoint::get_id(ep);

    // Created last, so the endpoints above keep their numbers. The other controls keep working without it.
    ep = create_mode_select_endpoint(node, "Radar Bluetooth", RADAR_BLUETOOTH_OFF);
    if (ep != nullptr) {
        s_bluetooth.endpoint_id = endpoint::get_id(ep);
    } else {
        ESP_LOGE(TAG, "Failed to create the Radar Bluetooth endpoint");
    }

    ESP_LOGI(TAG, "Endpoints: Learn empty room %u, Range %u, Radar resolution %u, Radar Bluetooth %u",
             s_learn.endpoint_id, s_range.endpoint_id, s_resolution.endpoint_id, s_bluetooth.endpoint_id);
    return ESP_OK;
}

esp_err_t radar_control_start(void)
{
    static const radar_worker_view_t view = {
        .show_settings = show_settings,
        .show_learn_mode = show_learn_mode,
    };
    return radar_worker_start(&view);
}

esp_err_t radar_control_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                   const esp_matter_attr_val_t *val)
{
    if (cluster_id != ModeSelect::Id || attribute_id != ModeSelect::Attributes::CurrentMode::Id) {
        return ESP_OK;
    }
    radar_select_t select;
    if (endpoint_id == s_learn.endpoint_id) {
        select = RADAR_SELECT_LEARN;
    } else if (endpoint_id == s_range.endpoint_id) {
        select = RADAR_SELECT_RANGE;
    } else if (endpoint_id == s_resolution.endpoint_id) {
        select = RADAR_SELECT_RESOLUTION;
    } else if (endpoint_id == s_bluetooth.endpoint_id) {
        select = RADAR_SELECT_BLUETOOTH;
    } else {
        return ESP_OK;
    }
    return radar_worker_select(select, val->val.u8);
}
