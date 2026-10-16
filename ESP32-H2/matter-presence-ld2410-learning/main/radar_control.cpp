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
#include <nvs.h>
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

// Radar resolution modes: the LD2410 resolution codes
static constexpr uint8_t kAccuracyCoarse = 0; // 0.75 m gates
static constexpr uint8_t kAccuracyFine = 1;   // 0.2 m gates

static constexpr uint8_t kBluetoothOff = 0;
static constexpr uint8_t kBluetoothOn = 1;

static constexpr size_t kRangeOptionCount = LD2410_MAX_MAX_GATE - LD2410_MIN_MAX_GATE + 1;

static constexpr size_t kDistanceLabelLen = 12; // "5.25 m"
static constexpr size_t kCustomLabelLen = 48;   // "Moving 5.25 m, still 5.25 m"
static constexpr size_t kMacLabelLen = 18;      // "8F:27:2E:B8:0F:65"

// Root, occupancy sensor, Range, Radar resolution and Radar Bluetooth
static_assert(CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT >= 5,
              "CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT must be at least 5 (see sdkconfig.defaults)");

// Password of the HLKRadarTool app stored in the radar, empty: left as it is
static constexpr char kBtPassword[] = CONFIG_LD2410_BT_PASSWORD;

static constexpr bool is_valid_bt_password(const char *password)
{
    size_t len = 0;
    for (; password[len] != '\0'; len++) {
        if (password[len] < 0x21 || password[len] > 0x7E) {
            return false;
        }
    }
    return len == 0 || len == LD2410_BT_PASSWORD_LEN;
}
static_assert(is_valid_bt_password(kBtPassword),
              "CONFIG_LD2410_BT_PASSWORD: 6 printable ASCII characters without spaces, or empty");
static constexpr size_t kBtPasswordLen = sizeof(kBtPassword) - 1; // LD2410_BT_PASSWORD_LEN or 0

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
static ModeOption s_accuracy_options[2];
static ModeOption s_range_options[kRangeOptionCount + 1];
static char s_range_labels[kRangeOptionCount][kDistanceLabelLen];
static char s_range_custom_label[kCustomLabelLen];
// Off, On, then "Not available" (RADAR_MODE_CUSTOM) listed alone
static ModeOption s_bluetooth_options[3];

static ModeList s_learn = { chip::kInvalidEndpointId, s_learn_options, 2, 2, nullptr };
static ModeList s_accuracy = { chip::kInvalidEndpointId, s_accuracy_options, 2, 2, nullptr };
static ModeList s_range = { chip::kInvalidEndpointId, s_range_options, kRangeOptionCount, kRangeOptionCount,
                            s_range_custom_label };
static ModeList s_bluetooth = { chip::kInvalidEndpointId, s_bluetooth_options, 2, 2, nullptr };

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

// 0.2 m is only listed when the radar can change its resolution. Returns true
// when the list changed.
static bool set_accuracy_options(bool resolution_settable)
{
    const size_t count = resolution_settable ? 2 : 1;
    const bool changed = s_accuracy.count != count;
    s_accuracy.count = count;
    return changed;
}

// Off and On, or only "Not available" when the radar does not answer the
// Bluetooth query (LD2410 without Bluetooth). Returns true when the list
// changed.
static bool set_bluetooth_options(bool bluetooth_known)
{
    ModeOption *options = bluetooth_known ? &s_bluetooth_options[0] : &s_bluetooth_options[2];
    const size_t count = bluetooth_known ? 2 : 1;
    const bool changed = s_bluetooth.options != options || s_bluetooth.count != count;
    s_bluetooth.options = options;
    s_bluetooth.count = count;
    return changed;
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
        for (const ModeList *list : { &s_learn, &s_range, &s_accuracy, &s_bluetooth }) {
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

// Range (gate), Radar resolution (gate length) and Radar Bluetooth (off / on)
// chosen in Home Assistant, RADAR_KEEP once taken.
static std::atomic<int32_t> s_want_gate{RADAR_KEEP};
static std::atomic<int32_t> s_want_gate_cm{RADAR_KEEP};
static std::atomic<int32_t> s_want_bluetooth{RADAR_KEEP};

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
// Bluetooth of the radar. Off until it is turned on from Home Assistant: the
// choice is kept in NVS and the worker brings the radar back to it at boot
// (the LD2410 leaves the factory with Bluetooth on).
// Password: stored once in each radar whose Bluetooth is on (known by its
// Bluetooth MAC address, which the radar only reports while its Bluetooth is
// on), and again each time Bluetooth is turned on from Home Assistant. The
// radar cannot report its password.
// Every change is applied by a restart of the radar.

struct BtPasswordRecord {
    uint8_t mac[LD2410_MAC_LEN];          // radar that holds the password
    char password[LD2410_BT_PASSWORD_LEN]; // kBtPassword when it was stored
};

static constexpr const char *kNvsNamespace = "ld2410";
static constexpr const char *kNvsBtOnKey = "bt_on";
static constexpr const char *kNvsBtPasswordKey = "bt_password";

// Worker only (and radar_control_start() before the worker runs)
static bool s_bt_wanted; // chosen in Home Assistant, off until then
static BtPasswordRecord s_bt_record;
static bool s_bt_record_valid;
// The last Bluetooth on/off change, or password write, did not work: not
// tried again before the next boot or the next change from Home Assistant, so
// a radar that does not follow is not restarted over and over.
static bool s_bt_change_failed;
static bool s_bt_password_failed;

static const char *bt_password()
{
    return kBtPassword[0] != '\0' ? kBtPassword : nullptr;
}

static void load_bt_state()
{
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return; // nothing stored yet: Bluetooth off, no password written
    }
    uint8_t on = 0;
    size_t len = sizeof(on);
    s_bt_wanted = nvs_get_blob(nvs, kNvsBtOnKey, &on, &len) == ESP_OK && len == sizeof(on) && on != 0;
    len = sizeof(s_bt_record);
    s_bt_record_valid = nvs_get_blob(nvs, kNvsBtPasswordKey, &s_bt_record, &len) == ESP_OK &&
                        len == sizeof(s_bt_record);
    nvs_close(nvs);
}

static void store_nvs(const char *key, const void *data, size_t len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, key, data, len);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s in NVS, err:%d", key, err);
    }
}

static void set_bt_wanted(bool on)
{
    if (on != s_bt_wanted) {
        s_bt_wanted = on;
        const uint8_t value = on ? 1 : 0;
        store_nvs(kNvsBtOnKey, &value, sizeof(value));
    }
}

static void store_bt_record(const uint8_t mac[LD2410_MAC_LEN])
{
    memcpy(s_bt_record.mac, mac, LD2410_MAC_LEN);
    memcpy(s_bt_record.password, kBtPassword, kBtPasswordLen);
    s_bt_record_valid = true;
    // When not saved: kept for this boot, written again after the next one
    store_nvs(kNvsBtPasswordKey, &s_bt_record, sizeof(s_bt_record));
}

// The radar's Bluetooth is on and it has not received kBtPassword yet.
static bool bt_password_needed(const radar_settings_t &settings)
{
    if (bt_password() == nullptr || !settings.bluetooth_known || !settings.bluetooth_on) {
        return false;
    }
    return !s_bt_record_valid || memcmp(s_bt_record.mac, settings.bt_mac, LD2410_MAC_LEN) != 0 ||
           memcmp(s_bt_record.password, kBtPassword, kBtPasswordLen) != 0;
}

// ---------------------------------------------------------------------------
// Worker task

static radar_settings_t s_settings; // last settings read from the radar (s_synced)

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

static uint8_t bluetooth_mode(const radar_settings_t &settings)
{
    if (!settings.bluetooth_known) {
        return RADAR_MODE_CUSTOM;
    }
    return settings.bluetooth_on ? kBluetoothOn : kBluetoothOff;
}

// Shows the radar settings in the Range, Radar resolution and Radar Bluetooth
// selects.
static void publish(const radar_settings_t &settings)
{
    const ld2410_config_t &cfg = settings.config;
    const uint16_t gate_cm = settings.gate_cm;
    const bool range_option = is_range_option(cfg);
    char range_label[kCustomLabelLen] = "";
    if (!range_option) {
        char moving[kDistanceLabelLen];
        char still[kDistanceLabelLen];
        radar_format_distance((uint32_t)cfg.max_moving_gate * gate_cm, moving, sizeof(moving));
        radar_format_distance((uint32_t)cfg.max_still_gate * gate_cm, still, sizeof(still));
        snprintf(range_label, sizeof(range_label), "Moving %s, still %s", moving, still);
    }

    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        const bool labels_changed = set_range_labels(gate_cm);
        if (set_custom_option(s_range, range_option ? nullptr : range_label) || labels_changed) {
            report_supported_modes(s_range.endpoint_id);
        }
        if (set_accuracy_options(settings.resolution_settable)) {
            report_supported_modes(s_accuracy.endpoint_id);
        }
        if (set_bluetooth_options(settings.bluetooth_known) && s_bluetooth.endpoint_id != chip::kInvalidEndpointId) {
            report_supported_modes(s_bluetooth.endpoint_id);
        }
    }
    // report() takes the stack lock itself
    report_current_mode(s_range.endpoint_id, range_option ? cfg.max_moving_gate : RADAR_MODE_CUSTOM);
    report_current_mode(s_accuracy.endpoint_id, gate_cm == LD2410_FINE_GATE_CM ? kAccuracyFine : kAccuracyCoarse);
    if (s_bluetooth.endpoint_id != chip::kInvalidEndpointId) {
        report_current_mode(s_bluetooth.endpoint_id, bluetooth_mode(settings));
    }
}

static void log_settings(const char *what, const radar_settings_t &settings)
{
    const ld2410_config_t &cfg = settings.config;
    char duration[kCustomLabelLen];
    char moving[kDistanceLabelLen];
    char still[kDistanceLabelLen];
    radar_format_duration(cfg.no_one_duration_s, duration, sizeof(duration));
    radar_format_distance((uint32_t)cfg.max_moving_gate * settings.gate_cm, moving, sizeof(moving));
    radar_format_distance((uint32_t)cfg.max_still_gate * settings.gate_cm, still, sizeof(still));
    char mac[kMacLabelLen];
    radar_format_mac(settings.bt_mac, mac, sizeof(mac));
    ESP_LOGI(TAG, "%s: no-one duration %s, range moving %s (gate %u), still %s (gate %u), %u cm per gate%s, "
                  "Bluetooth %s%s", what, duration, moving, cfg.max_moving_gate, still, cfg.max_still_gate,
             settings.gate_cm, settings.resolution_settable ? "" : " (fixed)",
             !settings.bluetooth_known ? "not available" : settings.bluetooth_on ? "on, MAC " : "off",
             settings.bluetooth_known && settings.bluetooth_on ? mac : "");
}

static bool is_change(const radar_settings_request_t &want)
{
    return want.duration_s != RADAR_KEEP || want.gate != RADAR_KEEP || want.gate_cm != RADAR_KEEP ||
           want.bluetooth != RADAR_KEEP || want.bt_password != nullptr;
}

static esp_err_t s_session_err; // result of the last session

// Reads the radar settings, applies the requested ones and shows the result
// in Home Assistant. False when the radar did not answer as expected, so the
// settings must be read again.
static bool settings_session(const radar_settings_request_t &want)
{
    const bool change = is_change(want);
    radar_settings_t now = {};
    const esp_err_t err = radar_settings_session(&want, &now);
    s_session_err = err;
    if (err != ESP_OK) {
        if (change && s_synced) {
            ESP_LOGE(TAG, "Changing the radar settings failed, err:%d", err);
            publish(s_settings); // back to the settings last read from the radar
        }
        return err == ESP_ERR_NOT_SUPPORTED && s_synced; // nothing wrong with the radar
    }
    if (!s_synced || change) {
        log_settings(s_synced ? "New LD2410 settings" : "LD2410 settings", now);
    }
    if (want.bt_password != nullptr && now.bluetooth_on) {
        if (now.bt_password_set) {
            ESP_LOGI(TAG, "LD2410 Bluetooth password set (CONFIG_LD2410_BT_PASSWORD)");
            store_bt_record(now.bt_mac);
        } else {
            s_bt_password_failed = true;
        }
    }
    s_settings = now;
    s_synced = true;
    publish(now);
    return true;
}

// After a successful session: brings the radar's Bluetooth to the state
// chosen in Home Assistant (off by default), then gives the password to a
// radar whose Bluetooth is on, chosen or not (one that refused to turn off
// must not keep the factory password). False when the radar did not answer as
// expected.
static bool fix_bluetooth()
{
    if (!s_settings.bluetooth_known) {
        return true;
    }
    if (!s_bt_change_failed && s_settings.bluetooth_on != s_bt_wanted) {
        ESP_LOGI(TAG, "Turning the LD2410 Bluetooth %s (%s)", s_bt_wanted ? "on" : "off",
                 s_bt_wanted ? "as chosen in Home Assistant" : "default, or as chosen in Home Assistant");
        radar_settings_request_t fix = {};
        fix.bluetooth = s_bt_wanted ? kBluetoothOn : kBluetoothOff;
        fix.bt_password = s_bt_wanted && !s_bt_password_failed ? bt_password() : nullptr;
        const bool answered = settings_session(fix);
        if (!answered || s_settings.bluetooth_on != s_bt_wanted) {
            s_bt_change_failed = true;
        }
        if (!answered) {
            return false;
        }
    }
    if (!s_bt_password_failed && bt_password_needed(s_settings)) {
        radar_settings_request_t fix = {};
        fix.bt_password = bt_password();
        if (!settings_session(fix)) {
            s_bt_password_failed = true;
            return false;
        }
    }
    return true;
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
    bool stale = true; // the radar settings must be read (again)
    uint32_t retry_ms = kSyncRetryFirstMs;
    while (true) {
        // Settings first: a session takes a few seconds at most, a learning minutes.
        const int32_t bluetooth = s_want_bluetooth.exchange(RADAR_KEEP);
        if (bluetooth != RADAR_KEEP) {
            // Kept even if this session fails: applied again at the next boot.
            set_bt_wanted(bluetooth == kBluetoothOn);
            s_bt_change_failed = false;
            if (bluetooth == kBluetoothOn) {
                s_bt_password_failed = false;
            }
        }
        const radar_settings_request_t want = {
            .duration_s = RADAR_KEEP,
            .gate = s_want_gate.exchange(RADAR_KEEP),
            .gate_cm = s_want_gate_cm.exchange(RADAR_KEEP),
            .bluetooth = bluetooth,
            // Turning Bluetooth on from Home Assistant also stores the password.
            .bt_password = bluetooth == kBluetoothOn ? bt_password() : nullptr,
        };
        if (stale || is_change(want)) {
            stale = !settings_session(want);
            if (bluetooth != RADAR_KEEP && (stale || s_settings.bluetooth_on != (bluetooth == kBluetoothOn))) {
                s_bt_change_failed = true; // not retried before the next boot or the next choice
            }
        }
        if (!stale) {
            stale = !fix_bluetooth();
        }
        run_requested_learning();

        TickType_t wait = portMAX_DELAY;
        if (stale) {
            if (s_session_err == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Could not read the LD2410 settings (check the wire from GPIO%d to the radar RX pin), "
                              "next try in %" PRIu32 " s", CONFIG_LD2410_UART_TX_PIN, retry_ms / 1000);
            } else {
                ESP_LOGW(TAG, "The LD2410 did not take the settings as expected (err:%d), read again in %" PRIu32 " s",
                         s_session_err, retry_ms / 1000);
            }
            wait = pdMS_TO_TICKS(retry_ms);
            retry_ms = std::min(retry_ms * 2, kSyncRetryMaxMs);
        } else {
            retry_ms = kSyncRetryFirstMs;
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
    set_range_labels(LD2410_DEFAULT_GATE_CM);
    set_option(s_accuracy_options[0], "0.75 m", kAccuracyCoarse);
    set_option(s_accuracy_options[1], "0.2 m", kAccuracyFine);
    set_option(s_bluetooth_options[0], "Off", kBluetoothOff);
    set_option(s_bluetooth_options[1], "On", kBluetoothOn);
    set_option(s_bluetooth_options[2], "Not available", RADAR_MODE_CUSTOM);

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

    // Until the worker has read the radar, Range, Radar resolution and Radar
    // Bluetooth show the last persisted mode, or the LD2410 factory settings
    // (Bluetooth: off, the default the worker applies). Home Assistant sorts
    // the entities by name: Hold time, Learn empty room, Radar Bluetooth,
    // Radar resolution, Range.
    endpoint_t *ep = create_mode_select_endpoint(node, "Range", LD2410_MAX_MAX_GATE);
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Range endpoint"));
    s_range.endpoint_id = endpoint::get_id(ep);

    ep = create_mode_select_endpoint(node, "Radar resolution", kAccuracyCoarse);
    VerifyOrReturnError(ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the Radar resolution endpoint"));
    s_accuracy.endpoint_id = endpoint::get_id(ep);

    // Created last, so the endpoints above keep their numbers. The other
    // controls keep working without it.
    ep = create_mode_select_endpoint(node, "Radar Bluetooth", kBluetoothOff);
    if (ep != nullptr) {
        s_bluetooth.endpoint_id = endpoint::get_id(ep);
    } else {
        ESP_LOGE(TAG, "Failed to create the Radar Bluetooth endpoint");
    }

    ESP_LOGI(TAG, "Endpoints: Learn empty room %u, Range %u, Radar resolution %u, Radar Bluetooth %u",
             s_learn.endpoint_id, s_range.endpoint_id, s_accuracy.endpoint_id, s_bluetooth.endpoint_id);
    return ESP_OK;
}

esp_err_t radar_control_start(void)
{
    load_bt_state();
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
    } else if (endpoint_id == s_range.endpoint_id || endpoint_id == s_accuracy.endpoint_id ||
               endpoint_id == s_bluetooth.endpoint_id) {
        if (mode == RADAR_MODE_CUSTOM) {
            return ESP_OK; // the radar's own value: nothing to change
        }
        if (task == nullptr || !s_synced) {
            // Rejected, so Home Assistant keeps showing the current value.
            ESP_LOGW(TAG, "The LD2410 settings have not been read yet, change ignored");
            return ESP_ERR_INVALID_STATE;
        }
        if (endpoint_id == s_range.endpoint_id) {
            VerifyOrReturnError(mode >= LD2410_MIN_MAX_GATE && mode <= LD2410_MAX_MAX_GATE, ESP_ERR_INVALID_ARG);
            s_want_gate = mode;
        } else if (endpoint_id == s_accuracy.endpoint_id) {
            VerifyOrReturnError(mode == kAccuracyCoarse || mode == kAccuracyFine, ESP_ERR_INVALID_ARG);
            s_want_gate_cm = mode == kAccuracyFine ? LD2410_FINE_GATE_CM : LD2410_DEFAULT_GATE_CM;
        } else {
            VerifyOrReturnError(mode == kBluetoothOff || mode == kBluetoothOn, ESP_ERR_INVALID_ARG);
            s_want_bluetooth = mode;
        }
    } else {
        return ESP_OK;
    }
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
    return ESP_OK;
}
