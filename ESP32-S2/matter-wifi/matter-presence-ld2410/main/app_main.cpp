/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Occupancy Sensor (device type 0x0107) on ESP32-S2, fed by an
// HLK-LD2410 24 GHz presence radar. The Occupancy Sensing cluster has the
// Radar feature and the HoldTime attribute, so controllers can tune how long
// the room stays "occupied" after the radar stops seeing anyone.
//
// ESP32-S2 has no Bluetooth: the device joins Wi-Fi through a setup page
// (wifi_setup.cpp) and is then commissioned on-network.

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model_provider/clusters/occupancy_sensing/integration.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <atomic>

#include <device_info.h>
#include <diag.h>
#include <drivers/ld2410.h>
#include <drivers/reset_button.h>
#include <drivers/status_led.h>
#include <wifi_setup.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr uint64_t kRadarPollUs = 100 * 1000;
static constexpr uint32_t kFactoryResetHoldMs = 5000;

static std::atomic<bool> s_matter_started{false};
static uint16_t s_occupancy_endpoint_id;

// ---------------------------------------------------------------------------
// Radar -> occupancy

// Runs on the Matter thread. The cluster applies HoldTime itself: occupied is
// reported at once, unoccupied only after HoldTime seconds without presence.
static void report_occupancy(bool present)
{
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    if (cluster) {
        cluster->SetOccupancy(present);
    }
}

static bool radar_presence(const ld2410_target_t &t)
{
    if (CONFIG_PRESENCE_MAX_DISTANCE_CM == 0) {
        return t.moving || t.still;
    }
    return (t.moving && t.moving_cm <= CONFIG_PRESENCE_MAX_DISTANCE_CM) ||
           (t.still && t.still_cm <= CONFIG_PRESENCE_MAX_DISTANCE_CM);
}

static void radar_poll_cb(void *arg)
{
    static bool s_radar_ok = true;
    static bool s_present = false;
    static bool s_reported = false;
    static bool s_reported_present = false;

    ld2410_target_t t = {};
    const bool has_data = ld2410_get_target(&t);
    const bool fresh = has_data && esp_timer_get_time() / 1000 - t.timestamp_ms <= CONFIG_RADAR_TIMEOUT_SEC * 1000;
    if (fresh != s_radar_ok) {
        s_radar_ok = fresh;
        if (fresh) {
            ESP_LOGI(TAG, "Radar data received");
        } else {
            ESP_LOGE(TAG, "No data from the LD2410 for %d s, check the wiring (radar TX -> GPIO%d, RX -> GPIO%d)",
                     CONFIG_RADAR_TIMEOUT_SEC, CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
        }
    }

    // A silent radar counts as "no one", so the room does not stay occupied forever.
    const bool present = fresh && radar_presence(t);
    if (present != s_present) {
        s_present = present;
        ESP_LOGI(TAG, "Radar: %s (moving %s %u cm, still %s %u cm)", present ? "presence" : "no one",
                 t.moving ? "yes" : "no", t.moving_cm, t.still ? "yes" : "no", t.still_cm);
        status_led_set_presence(present);
    }

    // Only transitions go to the cluster: each "unoccupied" restarts its hold timer.
    if (s_matter_started && (!s_reported || present != s_reported_present)) {
        s_reported = true;
        s_reported_present = present;
        chip::DeviceLayer::SystemLayer().ScheduleLambda([present]() { report_occupancy(present); });
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

    // HoldTime (and HoldTimeLimits, 1..300 s, default 30 s) is handled and
    // persisted by the cluster; Home Assistant shows it as a setting.
    cluster_t *cluster = cluster::get(endpoint, OccupancySensing::Id);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time(cluster, 30) != nullptr, nullptr);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time_limits(cluster, NULL, 0, 0) != nullptr,
                        nullptr);
    return endpoint;
}

static void update_led_mode()
{
    const bool commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    status_led_set_mode(commissioned ? STATUS_LED_PRESENCE : STATUS_LED_COMMISSIONING);
}

static void print_onboarding_codes_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);
    // No BLE on ESP32-S2: the controller finds the device on the local network.
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(commissionMgr.IsCommissioningWindowOpen() == false);

    // After removing the last fabric the Wi-Fi credentials are kept, so the
    // device can be added again on the network right away.
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                                                chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        print_onboarding_codes_if_necessary();
        break;

    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        if (event->Platform.ESPSystemEvent.Base == WIFI_EVENT &&
            event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_START) {
            wifi_setup_limit_tx_power();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "IP address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        print_onboarding_codes_if_necessary();
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

    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
    // Wi-Fi may already be running (STA_START delivered before this point).
    wifi_setup_limit_tx_power();
    return ESP_OK;
}

static void factory_reset_cb()
{
    ESP_LOGW(TAG, "Factory reset: erasing Wi-Fi and Matter pairing");
    status_led_set_mode(STATUS_LED_WIFI_SETUP);
    if (s_matter_started) {
        esp_matter::factory_reset(); // also clears the Wi-Fi network, then restarts
    } else {
        // Matter did not start: drop the Wi-Fi network only, keep the
        // diagnostics, and go back to the setup page.
        if (esp_wifi_restore() != ESP_OK) {
            nvs_flash_erase();
        }
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

    diag_record_boot();

    status_led_init(CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_ACTIVE_HIGH);
    if (!wifi_setup_is_provisioned()) {
        // Nothing to reset yet: the button is only enabled once Wi-Fi is set.
        status_led_set_mode(STATUS_LED_WIFI_SETUP);
        wifi_setup_run_portal(); // restarts once Wi-Fi is configured
    }
    reset_button_init(CONFIG_RESET_BUTTON_GPIO, kFactoryResetHoldMs, factory_reset_cb);
    status_led_set_mode(STATUS_LED_COMMISSIONING);

    err = ld2410_init(CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the LD2410 driver, err:%d", err);
    }

    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running");
    }

    static esp_timer_handle_t s_radar_timer;
    const esp_timer_create_args_t timer_args = {
        .callback = radar_poll_cb,
        .name = "radar_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_radar_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_radar_timer, kRadarPollUs));
}
