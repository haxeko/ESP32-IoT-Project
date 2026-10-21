/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter over Wi-Fi Occupancy Sensor (device type 0x0107) on an ESP32-C3 SuperMini, fed by an HLK-LD2410C 24 GHz
// presence radar. The Occupancy Sensing cluster has the Radar feature and the HoldTime attribute, so controllers can
// tune how long the room stays "occupied" after the radar stops seeing anyone.
//
// The phone sends the Wi-Fi SSID and password over BLE during commissioning; afterwards the device talks to the
// controllers directly over the Wi-Fi LAN and BLE is released.
//
// Endpoints (their numbers follow the creation order and must not change: controllers that paired with an earlier
// firmware keep them across OTA updates):
//   0 Root Node: Basic Information (+ ProductLabel, SerialNumber), OTA Requestor, Wi-Fi network clusters (Wi-Fi RSSI
//     and UpTime reported again by diag_report.cpp)
//   1 Occupancy Sensor: Occupancy Sensing (Radar, HoldTime), Identify, Mode Select "Learn empty room"
//   2, 3, 4 Mode Select: "Range", "Radar resolution", "Radar Bluetooth" (radar_control.cpp)
//   5 Temperature Sensor: temperature of the ESP32-C3 chip (c3_temperature.cpp), only with
//     CONFIG_PRESENCE_CHIP_TEMPERATURE (off by default)

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model_provider/clusters/occupancy_sensing/integration.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#if CONFIG_ENABLE_CHIP_SHELL
#include <esp_matter_console.h>
#endif
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs_flash.h>
#include <platform/KeyValueStoreManager.h>
#include <stdio.h>

#include <atomic>

#include <drivers/ld2410.h>
#include <drivers/status_led.h>
#include <drivers/user_button.h>

#include "board.h"
#include "c3_temperature.h"
#include "device_info.h"
#include "diag_report.h"
#include "identity_diag.h"
#include "mem_monitor.h"
#include "net_watchdog.h"
#include "presence.h"
#include "radar_control.h"
#include "radar_worker.h"
#include "setup_code.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr uint32_t kFactoryResetHoldMs = 5000;
// Pairing mode opened by a double click, as long as the window opened at boot when not commissioned (15 min)
static constexpr uint32_t kPairingWindowSec = CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS;
// Window reopened after the last fabric is removed
static constexpr uint32_t kRecommissioningWindowSec = 300;

// HoldTime limits reported to controllers (HoldTimeLimits attribute)
static constexpr uint16_t kHoldTimeMin = 1;
static constexpr uint16_t kHoldTimeMax = 300;

// Key in the Matter key-value store (erased by a factory reset): set once the default HoldTime from menuconfig has
// been applied. Firmware 1.0.153-1.0.172 of branch claude/c3-wroom-esp-presence (ESP32 presence engine) set its own
// key: a board coming from it keeps its HoldTime too.
static const char kHoldTimeInitKey[] = "app/hold-init";
static const char kHoldTimeInitKeyEngine[] = "app/hold-init2";

#if CONFIG_STATUS_LED_TYPE_NONE
static const char kLedText[] = "none";
#elif CONFIG_STATUS_LED_TYPE_WS2812
static const char kLedText[] = "WS2812";
#elif CONFIG_STATUS_LED_ACTIVE_HIGH
static const char kLedText[] = "single colour, on when high";
#else
static const char kLedText[] = "single colour, on when low";
#endif

static std::atomic<bool> s_matter_started{false};
// Set on the Matter task once BLE has been shut down after commissioning (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING); it
// stays off until the next reboot.
static bool s_ble_released;
static uint16_t s_occupancy_endpoint_id;

// ---------------------------------------------------------------------------------------------------------------------
// Occupancy Sensing

// Presence task. The cluster belongs to the Matter stack, so take the stack lock: this waits until the Matter task
// is free instead of posting to its event queue, which reboots the device when the queue is full. The cluster
// applies HoldTime itself: occupied is reported at once, unoccupied only after HoldTime seconds without presence.
static bool report_occupancy(bool present)
{
    if (!s_matter_started) {
        return false;
    }
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    VerifyOrReturnValue(cluster != nullptr, false);
    cluster->SetOccupancy(present);
    return true;
}

static endpoint_t *create_occupancy_sensor(node_t *node)
{
    occupancy_sensor::config_t config;
    config.occupancy_sensing.feature_flags = cluster::occupancy_sensing::feature::radar::get_id();
    endpoint_t *endpoint = occupancy_sensor::create(node, &config, ENDPOINT_FLAG_NONE, NULL);
    VerifyOrReturnValue(endpoint != nullptr, nullptr);

    // HoldTime and HoldTimeLimits are served and persisted by the cluster (see apply_hold_time_config()); Home
    // Assistant shows HoldTime as a setting.
    cluster_t *cluster = cluster::get(endpoint, OccupancySensing::Id);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time(cluster, CONFIG_PRESENCE_HOLD_TIME_SEC) !=
                            nullptr,
                        nullptr);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time_limits(cluster, NULL, 0, 0) != nullptr,
                        nullptr);
    return endpoint;
}

// Matter task, once the server is up. The cluster starts with a fixed 30 s default; use the menuconfig default
// instead, but only until a controller writes its own value (the cluster persists and restores it).
static void apply_hold_time_config()
{
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    VerifyOrReturn(cluster != nullptr && cluster->IsHoldTimeEnabled());

    cluster->SetHoldTimeLimits({ .holdTimeMin = kHoldTimeMin,
                                 .holdTimeMax = kHoldTimeMax,
                                 .holdTimeDefault = CONFIG_PRESENCE_HOLD_TIME_SEC });

    auto &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
    uint8_t applied = 0;
    uint8_t applied_engine = 0;
    if ((kvs.Get(kHoldTimeInitKey, &applied, sizeof(applied)) != CHIP_NO_ERROR || !applied) &&
        (kvs.Get(kHoldTimeInitKeyEngine, &applied_engine, sizeof(applied_engine)) != CHIP_NO_ERROR ||
         !applied_engine)) {
        (void)cluster->SetHoldTime(CONFIG_PRESENCE_HOLD_TIME_SEC);
        applied = 1;
        (void)kvs.Put(kHoldTimeInitKey, &applied, sizeof(applied));
    }
    ESP_LOGI(TAG, "Hold time %u s (limits %u-%u s)", cluster->GetHoldTime(), kHoldTimeMin, kHoldTimeMax);
}

// ---------------------------------------------------------------------------------------------------------------------
// Commissioning

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

// Matter task. The LED blinks while the commissioning window is open (waiting to be added to an app) and stays dark
// otherwise; an empty room learning shows over this.
static void update_led_mode()
{
    const bool pairing = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    status_led_set_mode(pairing ? STATUS_LED_PAIRING : STATUS_LED_OFF);
}

// Matter stack lock held (Matter task). Opens the window with the board's own setup code.
static void open_commissioning_window(uint32_t timeout_sec)
{
    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissionMgr.IsCommissioningWindowOpen()) {
        ESP_LOGI(TAG, "Commissioning window already open");
        return;
    }
    // Once BLE is released the device is only found over the Wi-Fi LAN (DNS-SD over mDNS).
    const auto advertisement = s_ble_released ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                                              : chip::CommissioningWindowAdvertisement::kAllSupported;
    const CHIP_ERROR err =
        commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(timeout_sec), advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    ESP_LOGI(TAG, "Commissioning window open for %" PRIu32 " s (%s)", timeout_sec,
             s_ble_released ? "network only" : "BLE and network");
    setup_code_print(s_ble_released);
}

#if CONFIG_PRESENCE_WIFI_STAY_AWAKE
// Wi-Fi modem sleep (the ESP-IDF default) lets the station sleep between DTIM beacons and the access point keeps the
// frames for the device until then, so the messages of the controllers arrive later. Off by default: the radio
// listening all the time heats the chip (80-96 °C on a SuperMini). Turned off once BLE is released after
// commissioning; while BLE commissions, the default coexistence setup of ESP-IDF is kept. Matter task, when BLE is
// released and at each connection after that (the Wi-Fi driver may not run yet when BLE is released at boot).
static void keep_wifi_awake()
{
    wifi_ps_type_t ps;
    if (esp_wifi_get_ps(&ps) == ESP_OK && ps == WIFI_PS_NONE) {
        return;
    }
    const esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BLE released: Wi-Fi power save off");
    } else {
        ESP_LOGW(TAG, "Could not turn the Wi-Fi power save off, err:%d", err);
    }
}
#endif

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    identity_diag_on_event(event);

    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        apply_hold_time_config();
        diag_report_start();
#if CONFIG_PRESENCE_CHIP_TEMPERATURE
        c3_temperature_start();
#endif
        if (!is_commissioned()) {
            setup_code_print(false); // the SDK opened the boot commissioning window (BLE and network)
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange:
        ESP_LOGI(TAG, "Wi-Fi connectivity changed");
        break;

#if CONFIG_PRESENCE_WIFI_STAY_AWAKE
    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        if (s_ble_released && event->Platform.ESPSystemEvent.Base == WIFI_EVENT &&
            event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_CONNECTED) {
            keep_wifi_awake();
        }
        break;
#endif

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
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
        // After removing the last fabric the Wi-Fi credentials are kept, so the device can be added again over the
        // network right away.
        if (!is_commissioned()) {
            open_commissioning_window(kRecommissioningWindowSec);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        s_ble_released = true;
#if CONFIG_PRESENCE_WIFI_STAY_AWAKE
        keep_wifi_awake();
#endif
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

// Matter task. The Mode Select clusters of radar_control.cpp change with the ChangeToMode command (Home Assistant
// select entities). HoldTime writes are handled by the Occupancy Sensing cluster itself.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }
    return radar_control_pre_update(endpoint_id, cluster_id, attribute_id, val);
}

static esp_err_t start_matter()
{
    // Manufacturer / Model / Serial number shown by controllers
    device_info_init();

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    VerifyOrReturnError(node != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create Matter node"));
    device_info_add_attributes(node);

    endpoint_t *occupancy_ep = create_occupancy_sensor(node);
    VerifyOrReturnError(occupancy_ep != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create occupancy_sensor endpoint"));
    s_occupancy_endpoint_id = endpoint::get_id(occupancy_ep);
    ESP_LOGI(TAG, "Occupancy sensor created with endpoint_id %d", s_occupancy_endpoint_id);

    // The sensor keeps working without the Home Assistant controls.
    const bool radar_control = radar_control_create(node, occupancy_ep) == ESP_OK;
    if (!radar_control) {
        ESP_LOGE(TAG, "Failed to create the LD2410 controls");
    }

#if CONFIG_PRESENCE_CHIP_TEMPERATURE
    // Endpoint 5. Only after the radar controls, which would otherwise leave it a lower number.
    if (radar_control && c3_temperature_create() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the chip temperature sensor");
    }
#endif

    // Per-board setup code (sdkconfig.defaults.unique_setup); nothing to do in the default build
    setup_code_init();

    // Wi-Fi: the station is started by the Matter stack with the credentials from commissioning
    const esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
    // First boot of a unique_setup build: generate the setup code on the Matter task (nothing in the default build)
    setup_code_preload();
    // Device name "Presence-B2C4": NodeLabel when none is stored yet, Bluetooth name
    device_info_apply_name();

    if (radar_control && radar_control_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the radar configuration task");
    }

    // Identity / Wi-Fi / Matter state at boot, on events and every CONFIG_IDENTITY_DIAG_PERIOD_SEC
    if (identity_diag_start(CONFIG_IDENTITY_DIAG_PERIOD_SEC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the identity diagnostics");
    }

    // Brings the device back when no controller subscribes any more (DNS-SD, Wi-Fi reconnect, restart)
    if (net_watchdog_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the connection watchdog");
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif
    return ESP_OK;
}

// ---------------------------------------------------------------------------------------------------------------------
// Button (callbacks in the esp_timer task: they must not block)

// Double click: pairing mode. Not commissioned: the device can be added again (e.g. after the boot window timed
// out). Commissioned: another app can add it with the same setup code (Multi-Admin). The work is handed to the
// Matter task instead of waiting here for the Matter stack lock, which would stall every esp_timer callback.
static void pairing_button_cb()
{
    VerifyOrReturn(s_matter_started, ESP_LOGW(TAG, "Matter is not running, cannot open pairing mode"));
    const CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        open_commissioning_window(kPairingWindowSec);
        update_led_mode();
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Pairing mode ignored, Matter task busy: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// Triple click: empty room learning, like "Learning" in Home Assistant; the radar_cfg task does the work.
static void learning_button_cb()
{
    radar_worker_request_learning();
}

// Four clicks: the LD2410 factory thresholds, like "Factory thresholds" in Home Assistant.
static void factory_thresholds_button_cb()
{
    radar_worker_request_factory_thresholds();
}

// Long press: factory reset.
static void factory_reset_cb()
{
    ESP_LOGW(TAG, "Factory reset: erasing the Wi-Fi network and Matter pairing");
    if (s_matter_started) {
        esp_matter::factory_reset(); // also clears the Wi-Fi credentials, then restarts
    } else {
        nvs_flash_erase();
        esp_restart();
    }
}

// ---------------------------------------------------------------------------------------------------------------------

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
    ESP_LOGI(TAG, "Target %s, board %s, Matter over Wi-Fi", CONFIG_IDF_TARGET, BOARD_NAME);
    char button[12] = "none";
    if (CONFIG_RESET_BUTTON_GPIO >= 0) {
        snprintf(button, sizeof(button), "GPIO%d", CONFIG_RESET_BUTTON_GPIO);
    }
#if CONFIG_STATUS_LED_TYPE_NONE
    ESP_LOGI(TAG, "Pins: LD2410 RX GPIO%d (radar TX), TX GPIO%d (radar RX), button %s, LED %s",
             CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN, button, kLedText);
#else
    ESP_LOGI(TAG, "Pins: LD2410 RX GPIO%d (radar TX), TX GPIO%d (radar RX), button %s, LED GPIO%d (%s)",
             CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN, button, CONFIG_STATUS_LED_GPIO, kLedText);
#endif

    status_led_init();
    const user_button_config_t button_config = {
        .on_double_click = pairing_button_cb,
        .on_triple_click = learning_button_cb,
        .on_quadruple_click = factory_thresholds_button_cb,
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

    // Started after Matter, so its first report reaches the cluster.
    if (presence_start(report_occupancy) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the presence task");
    }

    if (mem_monitor_start(CONFIG_MEM_MONITOR_PERIOD_SEC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the memory log");
    }
}
