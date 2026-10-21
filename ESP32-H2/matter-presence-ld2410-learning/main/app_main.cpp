/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Occupancy Sensor (device type 0x0107) over Thread on ESP32-H2, or
// over Wi-Fi on ESP32-C3 (board.h), fed by an HLK-LD2410 24 GHz presence
// radar. The Occupancy Sensing cluster has the Radar feature and the HoldTime
// attribute, so controllers can tune how long the room stays "occupied" after
// the radar stops seeing anyone.
//
// The phone sends the network credentials (Thread dataset or Wi-Fi SSID and
// password) over BLE during commissioning; afterwards the device talks to the
// controllers through a Thread Border Router, or directly over the Wi-Fi LAN.
//
// Home Assistant controls of the LD2410 (radar_control.cpp), Mode Select
// clusters that Home Assistant shows as select entities and Apple Home
// ignores: "Learn empty room" on the occupancy endpoint (records the radar's
// background energy and raises the LD2410 thresholds above it, see
// quiet_learn.cpp; also started by a triple click on the button, four clicks
// write the factory thresholds back), "Range"
// (farthest detection gate), "Radar resolution" (gate length) and "Radar
// Bluetooth" (off by default) on endpoints of their own.
//
// With CONFIG_PRESENCE_ENERGY_FILTER (variant sdkconfig.defaults.espfilter),
// the ESP32 confirms the radar's detections from the energy of every gate
// while the room is empty (energy_filter.h). With CONFIG_PRESENCE_ESP_ENGINE
// (variant sdkconfig.defaults.esppresence), the ESP32 decides presence itself
// from the energy of every gate (esp_presence.h, presence_engine.h).

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
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <platform/KeyValueStoreManager.h>
#include <stdio.h>

#if CONFIG_OPENTHREAD_ENABLED
#include <esp_openthread_types.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif
#if CONFIG_ENABLE_WIFI_STATION
#include <esp_wifi.h>
#endif

#include <atomic>

#include <board.h>
#include <device_info.h>
#include <drivers/ld2410.h>
#include <drivers/status_led.h>
#include <drivers/user_button.h>
#include <identity_diag.h>
#include <mem_monitor.h>
#include <presence.h>
#include <radar_control.h>
#include <setup_code.h>
#include <thread_diag.h>
#if CONFIG_PRESENCE_ENERGY_FILTER
#include <energy_filter.h>
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
#include <esp_presence.h>
#endif

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr uint32_t kRadarPollMs = 100;
static constexpr uint32_t kPresenceTaskStack = 4096;
// Above the Matter task (CONFIG_CHIP_TASK_PRIORITY = 1) so radar sampling keeps
// its pace, below OpenThread (5) or the Wi-Fi and lwIP tasks; the task sleeps
// between polls.
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
// default HoldTime from menuconfig has been applied. With the ESP32 presence
// engine, HoldTime is its absence countdown (60 s in that variant), applied
// once more under its own key when a board comes to this firmware; a value
// set afterwards in an app is kept.
#if CONFIG_PRESENCE_ESP_ENGINE
static const char kHoldTimeInitKey[] = "app/hold-init2";
#else
static const char kHoldTimeInitKey[] = "app/hold-init";
#endif

// Catch pin mistakes from menuconfig at build time: the board's flash (and on
// ESP32-C3 USB) pins, see board_<target>.h.
static constexpr bool is_reserved_pin(int gpio)
{
    return BOARD_PIN_RESERVED(gpio);
}
static_assert(CONFIG_LD2410_UART_RX_PIN != CONFIG_LD2410_UART_TX_PIN, "LD2410 RX and TX must be different pins");
static_assert(!is_reserved_pin(CONFIG_LD2410_UART_RX_PIN) && !is_reserved_pin(CONFIG_LD2410_UART_TX_PIN),
              "LD2410 pins must not be " BOARD_PIN_RESERVED_TEXT);
static_assert(CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                  CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN && !is_reserved_pin(CONFIG_RESET_BUTTON_GPIO),
              "Button pin conflicts with the radar or " BOARD_PIN_RESERVED_TEXT);
#if !CONFIG_STATUS_LED_TYPE_NONE
static_assert(CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_RX_PIN && CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  CONFIG_STATUS_LED_GPIO != CONFIG_RESET_BUTTON_GPIO && !is_reserved_pin(CONFIG_STATUS_LED_GPIO),
              "Status LED pin conflicts with the radar, the button or " BOARD_PIN_RESERVED_TEXT);
#endif

#if CONFIG_STATUS_LED_TYPE_WS2812
static const char kLedText[] = "WS2812";
#elif CONFIG_STATUS_LED_TYPE_GPIO && CONFIG_STATUS_LED_ACTIVE_HIGH
static const char kLedText[] = "single colour, on when high";
#elif CONFIG_STATUS_LED_TYPE_GPIO
static const char kLedText[] = "single colour, on when low";
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

#if CONFIG_PRESENCE_ESP_ENGINE
// The ESP32 presence engine decides (esp_presence.cpp, which logs why); the
// presence task hands its occupancy to the cluster and the cluster's HoldTime
// to the engine (the absence countdown of both). A silent radar is a sensor
// fault for the engine, not "no one": the occupancy then stays as it is.
static constexpr uint32_t kHoldTimeReadPolls = 10; // HoldTime read every second

// Presence task: the HoldTime the cluster serves (set from an app or by
// apply_hold_time_config()). False when it cannot be read yet.
static bool read_hold_time(uint16_t *out)
{
    if (!s_matter_started) {
        return false;
    }
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    VerifyOrReturnValue(cluster != nullptr && cluster->IsHoldTimeEnabled(), false);
    *out = cluster->GetHoldTime();
    return true;
}

static void presence_task(void *arg)
{
    bool reported = false;
    bool reported_present = false;
    uint32_t polls = 0;
    ESP_LOGI(TAG, "Occupancy from the ESP32 presence engine");
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kRadarPollMs));
        uint16_t hold_s = 0;
        if (polls++ % kHoldTimeReadPolls == 0 && read_hold_time(&hold_s)) {
            esp_presence_set_hold_time(hold_s);
        }
        // Occupied, or present but counting down (false: the cluster counts
        // HoldTime from then on, as the engine)
        const bool present = esp_presence_occupied();
        // Only transitions go to the cluster; a report that does not reach it
        // is retried on the next poll.
        if (s_matter_started && (!reported || present != reported_present) && report_occupancy(present)) {
            reported = true;
            reported_present = present;
        }
    }
}
#else
#if CONFIG_PRESENCE_ENERGY_FILTER
// Logs what the energy filter did since the last poll (one event per poll at most).
static void log_filter_event()
{
    energy_filter_state_t ev;
    if (!energy_filter_take_event(&ev)) {
        return;
    }
    const energy_filter_peak_t &p = ev.peak;
    const char *kind = p.moving ? "moving" : "still";
    switch (ev.event) {
    case ENERGY_FILTER_STRONG:
        ESP_LOGI(TAG, "Energy filter: %s gate %u energy %u, threshold %u (+%d): occupied at once", kind, p.gate,
                 p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_CONFIRMED:
        ESP_LOGI(TAG, "Energy filter: %u reports in a row above a threshold (highest %s gate %u energy %u, "
                      "threshold %u, +%d): occupied",
                 ev.event_frames, kind, p.gate, p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_IGNORED:
        ESP_LOGI(TAG, "Energy filter: ignored %u report%s above a threshold (highest %s gate %u energy %u, "
                      "threshold %u, +%d)",
                 ev.event_frames, ev.event_frames == 1 ? "" : "s", kind, p.gate, p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_NO_EVENT:
        break;
    }
}
#endif

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

#if CONFIG_PRESENCE_ENERGY_FILTER
        // The energy filter's decision once it has the radar's thresholds, the radar's until then.
        log_filter_event();
        bool filtered = false;
        const bool target = energy_filter_presence(&filtered) ? filtered
                                                               : presence_from_target(&t, CONFIG_PRESENCE_MAX_DISTANCE_CM);
#else
        const bool target = presence_from_target(&t, CONFIG_PRESENCE_MAX_DISTANCE_CM);
#endif
        // A silent radar counts as "no one", so the room does not stay occupied forever.
        const bool now_present = fresh && target;
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
#endif // CONFIG_PRESENCE_ESP_ENGINE

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
// is open (waiting to be added to an app) and stays dark otherwise; an empty
// room learning (radar_control.cpp) shows over this.
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
    // through the SRP server of the Thread Border Router, or mDNS on the Wi-Fi
    // LAN.
    const auto advertisement = s_ble_released ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                                              : chip::CommissioningWindowAdvertisement::kAllSupported;
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(timeout_sec), advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    ESP_LOGI(TAG, "Commissioning window open for %" PRIu32 " s (%s)", timeout_sec,
             s_ble_released ? "network only" : "BLE and network");
    setup_code_print(s_ble_released);
}

static void open_commissioning_window_if_necessary()
{
    // After removing the last fabric the Thread or Wi-Fi credentials are kept,
    // so the device can be added again over the network right away.
    VerifyOrReturn(!is_commissioned());
    open_commissioning_window(kRecommissioningWindowSec);
}

#if CONFIG_PRESENCE_WIFI_STAY_AWAKE
// Wi-Fi modem sleep (the ESP-IDF default) lets the station sleep between DTIM
// beacons and the access point keeps the frames for the device until then, so
// the acknowledgements of the controllers arrive late and Matter messages are
// sent again or time out. Turned off once BLE is released after commissioning;
// while BLE commissions, the default coexistence setup of ESP-IDF is kept.
// Called on the Matter task when BLE is released and at each connection after
// that (the Wi-Fi driver may not run yet when BLE is released at boot).
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
        // Already commissioned: no need for the periodic Thread diagnostics
        if (is_commissioned()) {
            thread_diag_stop();
        } else {
            // The SDK opened the boot commissioning window (BLE and network)
            setup_code_print(false);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

#if CONFIG_OPENTHREAD_ENABLED
    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity changed");
        break;
#endif

#if CONFIG_ENABLE_WIFI_STATION
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
#endif

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

// Runs on the Matter task. The Mode Select clusters of radar_control.cpp are
// changed by the ChangeToMode command (Home Assistant select entities). HoldTime
// writes are handled by the Occupancy Sensing cluster itself.
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
    /* Manufacturer / Model / Serial number shown by controllers */
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

#if CONFIG_OPENTHREAD_ENABLED
    /* OpenThread on the ESP32-H2 native 802.15.4 radio (no RCP, no host) */
    esp_openthread_platform_config_t ot_config = {
        .radio_config = { .radio_mode = RADIO_MODE_NATIVE },
        .host_config = { .host_connection_mode = HOST_CONNECTION_MODE_NONE },
        .port_config = { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10 },
    };
    set_openthread_platform_config(&ot_config);
#endif
    /* Wi-Fi (ESP32-C3): station started by the Matter stack with the credentials from commissioning */

    // Per-board setup code (sdkconfig.defaults.unique_setup); nothing to do in the default build
    setup_code_init();

    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
    // First boot of a unique_setup build: generate the setup code on the Matter task (nothing in the default build)
    setup_code_preload();
    // Device name "Presence-B2C3": NodeLabel when none is stored yet, Bluetooth name
    device_info_apply_name();

    if (radar_control && radar_control_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the radar configuration task");
    }

    /* Log Thread addresses / SRP state until commissioned (see README, troubleshooting); nothing on Wi-Fi */
    thread_diag_start();

    /* Identity / network / Matter state at boot, on events and every CONFIG_IDENTITY_DIAG_PERIOD_SEC */
    if (identity_diag_start(CONFIG_IDENTITY_DIAG_PERIOD_SEC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the identity diagnostics");
    }

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
// with the same setup code (Multi-Admin). Runs in the esp_timer task: the work is handed to the Matter
// task instead of waiting here for the Matter stack lock, which would stall every esp_timer callback.
static void pairing_button_cb()
{
    VerifyOrReturn(s_matter_started, ESP_LOGW(TAG, "Matter is not running, cannot open pairing mode"));
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        open_commissioning_window(kPairingWindowSec);
        update_led_mode();
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Pairing mode ignored, Matter task busy: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// Triple click: empty room learning, like "Learning" in Home Assistant. Runs in
// the esp_timer task: the radar task does the work and the LED shows it.
static void learning_button_cb()
{
    radar_control_request_learning();
}

// Four clicks: the LD2410 factory thresholds, like "Factory thresholds" in Home
// Assistant. Runs in the esp_timer task: the radar task writes them and the LED
// blinks purple (or shows red).
static void factory_thresholds_button_cb()
{
    radar_control_request_factory_thresholds();
}

// Long press: factory reset. Runs in the esp_timer task.
static void factory_reset_cb()
{
    ESP_LOGW(TAG, "Factory reset: erasing the " BOARD_NETWORK " network and Matter pairing");
    if (s_matter_started) {
        esp_matter::factory_reset(); // also clears the Thread dataset or the Wi-Fi credentials, then restarts
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
    ESP_LOGI(TAG, "Target %s, board %s, Matter over %s", CONFIG_IDF_TARGET, BOARD_NAME, BOARD_NETWORK);
    char button[12] = "none";
    if (CONFIG_RESET_BUTTON_GPIO >= 0) {
        snprintf(button, sizeof(button), "GPIO%d", CONFIG_RESET_BUTTON_GPIO);
    }
#if CONFIG_STATUS_LED_TYPE_NONE
    ESP_LOGI(TAG, "Pins: LD2410 RX GPIO%d (radar TX), TX GPIO%d (radar RX), button %s, no LED",
             CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN, button);
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
#if CONFIG_PRESENCE_ENERGY_FILTER
    // Fed by every report; it decides once the radar task gives it the radar's thresholds and Range.
    energy_filter_start();
    ESP_LOGI(TAG, "ESP32 energy filter on: an energy %d above a threshold occupies the empty room at once, weaker "
                  "ones after %d reports in a row",
             CONFIG_PRESENCE_ENERGY_FILTER_STRONG, CONFIG_PRESENCE_ENERGY_FILTER_FRAMES);
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
    // Before the radar task (started with Matter), which hands it the radar settings
    if (esp_presence_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the ESP32 presence engine");
    }
#endif

    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running");
    }

    // Started after Matter so that its first report sees s_matter_started.
    if (xTaskCreate(presence_task, "presence", kPresenceTaskStack, NULL, kPresenceTaskPriority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the presence task");
    }
#if CONFIG_PRESENCE_ESP_ENGINE
    // After Matter, which needs the most memory while it starts
    if (esp_presence_console_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the console of the presence engine");
    }
#endif

    if (mem_monitor_start(CONFIG_MEM_MONITOR_PERIOD_SEC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the memory log");
    }
}
