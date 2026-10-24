/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "matter_device.h"

#include <inttypes.h>
#include <stdio.h>

#include <atomic>

#include <esp_log.h>
#include <esp_matter.h>
#if CONFIG_ENABLE_CHIP_SHELL
#include <esp_matter_console.h>
#endif
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/link.h>
#include <openthread/thread.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model_provider/clusters/occupancy_sensing/integration.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "sdkconfig.h"

#include "ble_scanner.h"
#include "presence_config.h"

static const char *TAG = "matter";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using chip::DeviceLayer::ConnectivityManager;
using chip::DeviceLayer::ConnectivityMgr;

// Window reopened after the last fabric is removed
static constexpr uint32_t kRecommissioningWindowSec = 300;
// Matter "Common Number" semantic tag namespace (data_model/1.6.1/namespaces/Namespace-Common-Number.xml)
static constexpr uint8_t kNamespaceCommonNumber = 0x07;

static uint16_t s_endpoint_ids[PRESENCE_SLOT_COUNT];
static std::atomic<bool> s_started{ false };
static std::atomic<uint32_t> s_attaches{ 0 };
static std::atomic<uint32_t> s_detaches{ 0 };

// TagList: the labels must stay valid for the life of the node
static char s_tag_labels[PRESENCE_SLOT_COUNT][16];
static chip::app::DataModel::Provider::SemanticTag s_tags[PRESENCE_SLOT_COUNT];

static uint32_t occupancy_feature()
{
#if CONFIG_PRESENCE_OCCUPANCY_FEATURE_OTHER
    return cluster::occupancy_sensing::feature::other::get_id();
#else
    return cluster::occupancy_sensing::feature::rf_sensing::get_id();
#endif
}

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

static void hold_off_scanner()
{
    ble_scanner_hold_off(CONFIG_PRESENCE_SCAN_RESUME_DELAY_SEC * 1000);
}

// Matter task
static void update_commissioning_gate()
{
    if (is_commissioned()) {
        ble_scanner_resume(SCAN_PAUSE_NOT_COMMISSIONED);
    } else {
        ble_scanner_pause(SCAN_PAUSE_NOT_COMMISSIONED);
    }
}

// Matter task
static void set_thread_attached(bool attached)
{
    if (attached) {
        hold_off_scanner();
        ble_scanner_resume(SCAN_PAUSE_THREAD_DETACHED);
    } else {
        ble_scanner_pause(SCAN_PAUSE_THREAD_DETACHED);
    }
}

static const char *thread_role()
{
    otInstance *ot = esp_openthread_get_instance();
    if (ot == nullptr) {
        return "none";
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    const char *role = otThreadDeviceRoleToString(otThreadGetDeviceRole(ot));
    esp_openthread_lock_release();
    return role;
}

static const char *device_type_name(ConnectivityManager::ThreadDeviceType type)
{
    switch (type) {
    case ConnectivityManager::kThreadDeviceType_Router:
        return "router";
    case ConnectivityManager::kThreadDeviceType_FullEndDevice:
        return "full end device";
    case ConnectivityManager::kThreadDeviceType_MinimalEndDevice:
        return "minimal end device";
    case ConnectivityManager::kThreadDeviceType_SleepyEndDevice:
        return "sleepy end device";
    default:
        return "other";
    }
}

// Matter task
static void on_server_ready()
{
    // MTD build: Minimal End Device keeps the receiver on when idle (no polling delay; the device is mains
    // powered). It is the OpenThread default for an MTD, set explicitly so a future SDK default cannot change it.
    const CHIP_ERROR err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Setting the Thread device type failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
    ESP_LOGI(TAG, "Thread device type: %s", device_type_name(ConnectivityMgr().GetThreadDeviceType()));

    ble_scanner_resume(SCAN_PAUSE_MATTER_STARTING);
    update_commissioning_gate();
    set_thread_attached(ConnectivityMgr().IsThreadAttached());
    if (!is_commissioned()) {
        ESP_LOGI(TAG, "Not commissioned: commissioning window open (BLE), beacon scanning paused until done");
        PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
    } else {
        ESP_LOGI(TAG, "Commissioned (%u fabric(s)), Thread %s",
                 static_cast<unsigned>(chip::Server::GetInstance().GetFabricTable().FabricCount()), thread_role());
    }
}

// Matter task
static void open_commissioning_window()
{
    chip::CommissioningWindowManager &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen()) {
        return;
    }
    // BLE is still initialised (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=n): advertise over BLE and DNS-SD
    const CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(kRecommissioningWindowSec),
                                                            chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open the commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;
    switch (event->Type) {
    case DeviceEventType::kServerReady:
        on_server_ready();
        break;

    case DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started: scanner paused");
        ble_scanner_pause(SCAN_PAUSE_COMMISSIONING);
        break;

    case DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        hold_off_scanner();
        ble_scanner_resume(SCAN_PAUSE_COMMISSIONING);
        break;

    case DeviceEventType::kCHIPoBLEConnectionEstablished:
        ESP_LOGI(TAG, "BLE connection from a controller: scanner paused");
        ble_scanner_pause(SCAN_PAUSE_BLE_CONNECTION);
        break;

    case DeviceEventType::kCHIPoBLEConnectionClosed:
        ESP_LOGI(TAG, "BLE connection closed");
        hold_off_scanner();
        ble_scanner_resume(SCAN_PAUSE_BLE_CONNECTION);
        break;

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete: scanner resumes once Thread is attached");
        hold_off_scanner();
        ble_scanner_resume(SCAN_PAUSE_COMMISSIONING);
        update_commissioning_gate();
        break;

    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed: fail-safe timer expired");
        ble_scanner_resume(SCAN_PAUSE_COMMISSIONING);
        update_commissioning_gate();
        break;

    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        update_commissioning_gate();
        if (!is_commissioned()) {
            open_commissioning_window();
        }
        break;

    case DeviceEventType::kFabricCommitted:
        update_commissioning_gate();
        break;

    case DeviceEventType::kThreadConnectivityChange: {
        const bool attached = event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established;
        if (attached) {
            s_attaches.fetch_add(1);
            ESP_LOGI(TAG, "Thread attached (%s): scanning resumes in %d s", thread_role(),
                     CONFIG_PRESENCE_SCAN_RESUME_DELAY_SEC);
        } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            s_detaches.fetch_add(1);
            ESP_LOGW(TAG, "Thread detached: scanner paused until attached again");
        }
        set_thread_attached(attached);
        break;
    }

    case DeviceEventType::kThreadStateChange:
        if (event->ThreadStateChange.RoleChanged) {
            ESP_LOGI(TAG, "Thread role: %s", thread_role());
        }
        break;

    case DeviceEventType::kBLEDeinitialized:
        // Only with CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING, which this firmware turns off
        ESP_LOGE(TAG, "BLE was released by the Matter SDK: beacon scanning is no longer possible");
        break;

    default:
        break;
    }
}

static esp_err_t identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                   uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify endpoint %u: type %u, effect %u", endpoint_id, type, effect_id);
    return ESP_OK;
}

static esp_err_t attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                     uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK; // no writable attribute drives anything here
}

static esp_err_t create_presence_endpoints(node_t *node)
{
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        occupancy_sensor::config_t config;
        config.occupancy_sensing.feature_flags = occupancy_feature();
        endpoint_t *ep = occupancy_sensor::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
        if (ep == nullptr) {
            ESP_LOGE(TAG, "Cannot create the Occupancy Sensor endpoint of Presence %d", i + 1);
            return ESP_FAIL;
        }
        s_endpoint_ids[i] = endpoint::get_id(ep);

        snprintf(s_tag_labels[i], sizeof(s_tag_labels[i]), "Presence %d", i + 1);
        s_tags[i].namespaceID = kNamespaceCommonNumber;
        s_tags[i].tag = static_cast<uint8_t>(i + 1);
        s_tags[i].label.SetValue(
            chip::app::DataModel::MakeNullable(chip::CharSpan::fromCharString(s_tag_labels[i])));
        if (endpoint::set_semantic_tags(ep, &s_tags[i], 1) != ESP_OK) {
            ESP_LOGW(TAG, "TagList not set on endpoint %u", s_endpoint_ids[i]);
        }
        if (s_endpoint_ids[i] != i + 1) {
            ESP_LOGW(TAG, "Presence %d got endpoint %u instead of %d", i + 1, s_endpoint_ids[i], i + 1);
        }
    }
    ESP_LOGI(TAG, "Occupancy Sensor endpoints %u-%u (Presence 1-5), feature %s", s_endpoint_ids[0],
             s_endpoint_ids[PRESENCE_SLOT_COUNT - 1],
#if CONFIG_PRESENCE_OCCUPANCY_FEATURE_OTHER
             "Other"
#else
             "RFSensing"
#endif
    );
    return ESP_OK;
}

esp_err_t matter_device_start(void)
{
    node::config_t node_config;
    node_t *node = node::create(&node_config, attribute_update_cb, identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create the Matter node");
        return ESP_FAIL;
    }
    esp_err_t err = create_presence_endpoints(node);
    if (err != ESP_OK) {
        return err;
    }

    esp_openthread_platform_config_t ot_config = {};
    ot_config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    ot_config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    ot_config.port_config.storage_partition_name = "nvs";
    ot_config.port_config.netif_queue_size = 10;
    ot_config.port_config.task_queue_size = 10;
    set_openthread_platform_config(&ot_config);

    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
    return ESP_OK;
}

bool matter_device_set_occupancy(int slot, bool occupied)
{
    if (!s_started || slot < 0 || slot >= PRESENCE_SLOT_COUNT) {
        return false;
    }
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_endpoint_ids[slot]);
    if (cluster == nullptr) {
        return false;
    }
    cluster->SetOccupancy(occupied);
    return true;
}

void matter_device_log_diagnostics(void)
{
    otInstance *ot = s_started ? esp_openthread_get_instance() : nullptr;
    if (ot == nullptr) {
        return;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    const otDeviceRole role = otThreadGetDeviceRole(ot);
    const otLinkModeConfig mode = otThreadGetLinkMode(ot);
    int8_t parent_rssi = 0;
    const bool has_parent_rssi = otThreadGetParentAverageRssi(ot, &parent_rssi) == OT_ERROR_NONE;
    const otMacCounters mac = *otLinkGetCounters(ot);
    const otMleCounters mle = *otThreadGetMleCounters(ot);
    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Thread %s, rx-on-when-idle %d, full device %d, parent RSSI %d dBm%s; attached %u, detached %u",
             otThreadDeviceRoleToString(role), mode.mRxOnWhenIdle, mode.mDeviceType, parent_rssi,
             has_parent_rssi ? "" : " (n/a)", static_cast<unsigned>(s_attaches.load()),
             static_cast<unsigned>(s_detaches.load()));
    ESP_LOGI(TAG,
             "MAC tx %" PRIu32 " retry %" PRIu32 " cca-err %" PRIu32 " no-ack-expiry %" PRIu32 ", rx %" PRIu32
             " no-frame %" PRIu32 " fcs-err %" PRIu32 "; MLE detached %u child %u attach-attempts %u parent-changes %u",
             mac.mTxTotal, mac.mTxRetry, mac.mTxErrCca, mac.mTxDirectMaxRetryExpiry, mac.mRxTotal, mac.mRxErrNoFrame,
             mac.mRxErrFcs, mle.mDetachedRole, mle.mChildRole, mle.mAttachAttempts, mle.mParentChanges);
}
