/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Occupancy Sensor (device type 0x0107) over Thread on ESP32-H2, fed
// by an HLK-LD2410 24 GHz presence radar. The Occupancy Sensing cluster has
// the Radar feature and the HoldTime attribute, so controllers can tune how
// long the room stays "occupied" after the radar stops seeing anyone.
//
// The phone sends the Thread network credentials over BLE during
// commissioning; afterwards the device talks to the controllers through a
// Thread Border Router.

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model_provider/clusters/occupancy_sensing/integration.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <platform/KeyValueStoreManager.h>

#include <esp_openthread_types.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include <atomic>

#include <device_info.h>
#include <drivers/ld2410.h>
#include <drivers/status_led.h>
#include <drivers/user_button.h>
#include <thread_diag.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr uint32_t kRadarPollMs = 100;
static constexpr uint32_t kPresenceTaskStack = 4096;
// Above the Matter task (CONFIG_CHIP_TASK_PRIORITY = 1) so radar sampling keeps
// its pace, below OpenThread (5); the task sleeps between polls.
static constexpr UBaseType_t kPresenceTaskPriority = 2;
static constexpr uint32_t kFactoryResetHoldMs = 5000;
// Pairing mode opened by a double click on the button, same length as the
// window opened at boot when the device is not commissioned (15 min)
static constexpr uint32_t kPairingWindowSec = CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS;
// Window reopened after the last fabric is removed
static constexpr uint32_t kRecommissioningWindowSec = 300;

// HoldTime limits reported to controllers (HoldTimeLimits attribute)
static constexpr uint16_t kHoldTimeMin = 1;
static constexpr uint16_t kHoldTimeMax = 300;

// Key in the Matter key-value store (erased by a factory reset): set once the
// default HoldTime from menuconfig has been applied.
static const char kHoldTimeInitKey[] = "app/hold-init";

// Catch pin mistakes from menuconfig at build time. GPIO15-GPIO21 connect the
// in-package flash of ESP32-H2.
static constexpr bool is_flash_pin(int gpio)
{
    return gpio >= 15 && gpio <= 21;
}
static_assert(CONFIG_LD2410_UART_RX_PIN != CONFIG_LD2410_UART_TX_PIN, "LD2410 RX and TX must be different pins");
static_assert(!is_flash_pin(CONFIG_LD2410_UART_RX_PIN) && !is_flash_pin(CONFIG_LD2410_UART_TX_PIN),
              "LD2410 pins must not be flash pins (GPIO15-GPIO21)");
static_assert(CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                  CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN && !is_flash_pin(CONFIG_RESET_BUTTON_GPIO),
              "Button pin conflicts with the radar or flash pins");
#if !CONFIG_STATUS_LED_TYPE_NONE
static_assert(CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_RX_PIN && CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  CONFIG_STATUS_LED_GPIO != CONFIG_RESET_BUTTON_GPIO && !is_flash_pin(CONFIG_STATUS_LED_GPIO),
              "Status LED pin conflicts with the radar, the button or flash pins");
#endif

static std::atomic<bool> s_matter_started{false};
// Set on the Matter task once BLE has been shut down after commissioning
// (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING); it stays off until the next reboot.
static bool s_ble_released;
static uint16_t s_occupancy_endpoint_id;

// ---------------------------------------------------------------------------
// Radar -> occupancy

// Called from the presence task: the cluster belongs to the Matter stack, so
// take the stack lock. This blocks until the Matter task is free instead of
// posting to its event queue, which reboots the device when the queue is full.
// The cluster applies HoldTime itself: occupied is reported at once,
// unoccupied only after HoldTime seconds without presence.
static bool report_occupancy(bool present)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    VerifyOrReturnValue(cluster != nullptr, false);
    cluster->SetOccupancy(present);
    return true;
}

static bool radar_presence(const ld2410_target_t &t)
{
    if (CONFIG_PRESENCE_MAX_DISTANCE_CM == 0) {
        return t.moving || t.still;
    }
    return (t.moving && t.moving_cm <= CONFIG_PRESENCE_MAX_DISTANCE_CM) ||
           (t.still && t.still_cm <= CONFIG_PRESENCE_MAX_DISTANCE_CM);
}

// Polls the radar every kRadarPollMs. Runs in its own task rather than an
// esp_timer callback: it logs and may wait for the Matter stack lock, which
// must not stall the high-priority esp_timer task.
static void presence_task(void *arg)
{
    bool radar_ok = true;
    bool present = false;
    bool reported = false;
    bool reported_present = false;

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kRadarPollMs));

        ld2410_target_t t = {};
        // Before the first frame, count from boot so the radar gets the same grace period.
        const int64_t last_ms = ld2410_get_target(&t) ? t.timestamp_ms : 0;
        const bool fresh = esp_timer_get_time() / 1000 - last_ms <= CONFIG_RADAR_TIMEOUT_SEC * 1000;
        if (fresh != radar_ok) {
            radar_ok = fresh;
            if (fresh) {
                ESP_LOGI(TAG, "Radar data received");
            } else {
                ESP_LOGE(TAG, "No data from the LD2410 for %d s, check the wiring (radar TX -> GPIO%d, RX -> GPIO%d)",
                         CONFIG_RADAR_TIMEOUT_SEC, CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
            }
        }

        // A silent radar counts as "no one", so the room does not stay occupied forever.
        const bool now_present = fresh && radar_presence(t);
        if (now_present != present) {
            present = now_present;
            if (fresh) {
                ESP_LOGI(TAG, "Radar: %s (moving %s %u cm, still %s %u cm)", present ? "presence" : "no one",
                         t.moving ? "yes" : "no", t.moving_cm, t.still ? "yes" : "no", t.still_cm);
            } else {
                ESP_LOGI(TAG, "Radar: no one (no radar data)");
            }
        }

        // Only transitions go to the cluster: each "unoccupied" restarts its hold timer.
        // A report that does not reach the cluster is retried on the next poll.
        if (s_matter_started && (!reported || present != reported_present) && report_occupancy(present)) {
            reported = true;
            reported_present = present;
        }
    }
}

// ---------------------------------------------------------------------------
// Matter

static endpoint_t *create_occupancy_sensor(node_t *node)
{
    occupancy_sensor::config_t config;
    config.occupancy_sensing.feature_flags = cluster::occupancy_sensing::feature::radar::get_id();
    endpoint_t *endpoint = occupancy_sensor::create(node, &config, ENDPOINT_FLAG_NONE, NULL);
    VerifyOrReturnValue(endpoint != nullptr, nullptr);

    // HoldTime and HoldTimeLimits are served and persisted by the cluster
    // (see apply_hold_time_config()); Home Assistant shows HoldTime as a setting.
    cluster_t *cluster = cluster::get(endpoint, OccupancySensing::Id);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time(cluster, CONFIG_PRESENCE_HOLD_TIME_SEC) !=
                            nullptr,
                        nullptr);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time_limits(cluster, NULL, 0, 0) != nullptr,
                        nullptr);
    return endpoint;
}

// Runs on the Matter thread once the server is up. The cluster starts with a
// fixed 30 s default; use the menuconfig default instead, but only until a
// controller writes its own value (the cluster persists and restores it).
static void apply_hold_time_config()
{
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    VerifyOrReturn(cluster != nullptr && cluster->IsHoldTimeEnabled());

    cluster->SetHoldTimeLimits({ .holdTimeMin = kHoldTimeMin,
                                 .holdTimeMax = kHoldTimeMax,
                                 .holdTimeDefault = CONFIG_PRESENCE_HOLD_TIME_SEC });

    auto &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
    uint8_t applied = 0;
    if (kvs.Get(kHoldTimeInitKey, &applied, sizeof(applied)) != CHIP_NO_ERROR || !applied) {
        (void) cluster->SetHoldTime(CONFIG_PRESENCE_HOLD_TIME_SEC);
        applied = 1;
        (void) kvs.Put(kHoldTimeInitKey, &applied, sizeof(applied));
    }
    ESP_LOGI(TAG, "Hold time %u s (limits %u-%u s)", cluster->GetHoldTime(), kHoldTimeMin, kHoldTimeMax);
}

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

// Runs on the Matter task. The LED blinks blue while the commissioning window
// is open (waiting to be added to an app) and stays dark otherwise.
static void update_led_mode()
{
    const bool pairing = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    status_led_set_mode(pairing ? STATUS_LED_PAIRING : STATUS_LED_OFF);
}

// Must hold the Matter stack lock (Matter task or ScopedChipStackLock).
static void open_commissioning_window(uint32_t timeout_sec)
{
    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissionMgr.IsCommissioningWindowOpen()) {
        ESP_LOGI(TAG, "Commissioning window already open");
        return;
    }

    // Once BLE is released the device is only found over the network: DNS-SD
    // through the SRP server of the Thread Border Router.
    const auto advertisement = s_ble_released ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                                              : chip::CommissioningWindowAdvertisement::kAllSupported;
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(timeout_sec), advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    ESP_LOGI(TAG, "Commissioning window open for %" PRIu32 " s (%s)", timeout_sec,
             s_ble_released ? "network only" : "BLE and network");
}

static void open_commissioning_window_if_necessary()
{
    // After removing the last fabric the Thread credentials are kept, so the
    // device can be added again over the network right away.
    VerifyOrReturn(!is_commissioned());
    open_commissioning_window(kRecommissioningWindowSec);
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        apply_hold_time_config();
        // Already commissioned: no need for the periodic Thread diagnostics
        if (is_commissioned()) {
            thread_diag_stop();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        thread_diag_stop();
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        s_ble_released = true;
        break;

    default:
        break;
    }
    update_led_mode();
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    if (type == identification::START) {
        status_led_set_identify(true);
    } else if (type == identification::STOP) {
        status_led_set_identify(false);
    }
    return ESP_OK;
}

// HoldTime writes are handled by the Occupancy Sensing cluster itself.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

static esp_err_t start_matter()
{
    /* Manufacturer / Model / Serial number shown by controllers */
    device_info_init();

    node::config_t node_config;
    device_info_set_name(&node_config);
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    VerifyOrReturnError(node != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create Matter node"));
    device_info_add_attributes(node);

    endpoint_t *occupancy_ep = create_occupancy_sensor(node);
    VerifyOrReturnError(occupancy_ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create occupancy_sensor endpoint"));
    s_occupancy_endpoint_id = endpoint::get_id(occupancy_ep);
    ESP_LOGI(TAG, "Occupancy sensor created with endpoint_id %d", s_occupancy_endpoint_id);

    /* OpenThread on the ESP32-H2 native 802.15.4 radio (no RCP, no host) */
    esp_openthread_platform_config_t ot_config = {
        .radio_config = { .radio_mode = RADIO_MODE_NATIVE },
        .host_config = { .host_connection_mode = HOST_CONNECTION_MODE_NONE },
        .port_config = { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10 },
    };
    set_openthread_platform_config(&ot_config);

    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;

    /* Log Thread addresses / SRP state until commissioned (see README, troubleshooting) */
    thread_diag_start();

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
    return ESP_OK;
}

// Double click: pairing mode. Not commissioned: the device can be added again
// (e.g. after the boot window timed out). Commissioned: another app can add it
// with the same setup code (Multi-Admin). Runs in the esp_timer task.
static void pairing_button_cb()
{
    VerifyOrReturn(s_matter_started, ESP_LOGW(TAG, "Matter is not running, cannot open pairing mode"));
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    open_commissioning_window(kPairingWindowSec);
    update_led_mode();
}

// Long press: factory reset. Runs in the esp_timer task.
static void factory_reset_cb()
{
    ESP_LOGW(TAG, "Factory reset: erasing the Thread network and Matter pairing");
    if (s_matter_started) {
        esp_matter::factory_reset(); // also clears the Thread dataset, then restarts
    } else {
        nvs_flash_erase();
        esp_restart();
    }
}

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "%s %s, firmware %s", CONFIG_DEVICE_VENDOR_NAME, CONFIG_DEVICE_PRODUCT_NAME,
             esp_app_get_description()->version);

    status_led_init();
    const user_button_config_t button_config = {
        .on_double_click = pairing_button_cb,
        .on_long_press = factory_reset_cb,
        .long_press_ms = kFactoryResetHoldMs,
    };
    user_button_init(CONFIG_RESET_BUTTON_GPIO, &button_config);

    err = ld2410_init(CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the LD2410 driver, err:%d", err);
    }

    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running");
    }

    // Started after Matter so that its first report sees s_matter_started.
    if (xTaskCreate(presence_task, "presence", kPresenceTaskStack, NULL, kPresenceTaskPriority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the presence task");
    }
}
