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
#include <esp_bit_defs.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <soc/soc_caps.h>

#include <atomic>

#include <device_info.h>
#include <diag.h>
#include <drivers/ld2410.h>
#include <drivers/reset_button.h>
#include <drivers/status_led.h>
#include <presence.h>
#include <wifi_setup.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr uint64_t kRadarPollUs = 100 * 1000;
static constexpr uint32_t kFactoryResetHoldMs = 5000;
static constexpr unsigned kHeapLogPeriodSec = 10 * 60;

// ---------------------------------------------------------------------------
// Pins, checked at build time: a shared or unusable pin would only show up as
// a silent radar, a dead LED or a button that does nothing.

static constexpr bool pin_usable(int pin, bool output)
{
    const uint64_t valid = output ? SOC_GPIO_VALID_OUTPUT_GPIO_MASK : SOC_GPIO_VALID_GPIO_MASK;
#if CONFIG_IDF_TARGET_ESP32S2
    // GPIO26-32 drive the SPI flash and PSRAM. With the console on the native
    // USB port, GPIO19/20 are its D-/D+ lines.
    uint64_t reserved = BIT64(26) | BIT64(27) | BIT64(28) | BIT64(29) | BIT64(30) | BIT64(31) | BIT64(32);
#if CONFIG_ESP_CONSOLE_USB_CDC
    reserved |= BIT64(19) | BIT64(20);
#endif
#else
    const uint64_t reserved = 0;
#endif
    return pin >= 0 && pin < SOC_GPIO_PIN_COUNT && ((valid & ~reserved) >> pin & 1);
}

static_assert(pin_usable(CONFIG_LD2410_UART_RX_PIN, false), "LD2410 RX pin (radar TX) cannot be used on this chip");
static_assert(pin_usable(CONFIG_LD2410_UART_TX_PIN, true), "LD2410 TX pin (radar RX) cannot be used on this chip");
static_assert(CONFIG_LD2410_UART_RX_PIN != CONFIG_LD2410_UART_TX_PIN, "LD2410 RX and TX need two different pins");
static_assert(CONFIG_STATUS_LED_GPIO < 0 || pin_usable(CONFIG_STATUS_LED_GPIO, true),
              "Status LED pin cannot be used as an output on this chip");
static_assert(CONFIG_STATUS_LED_GPIO < 0 ||
                  (CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                   CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_TX_PIN),
              "Status LED pin is already used by the LD2410");
static_assert(CONFIG_RESET_BUTTON_GPIO < 0 || pin_usable(CONFIG_RESET_BUTTON_GPIO, false),
              "Reset button pin cannot be used on this chip");
static_assert(CONFIG_RESET_BUTTON_GPIO < 0 ||
                  (CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                   CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                   CONFIG_RESET_BUTTON_GPIO != CONFIG_STATUS_LED_GPIO),
              "Reset button pin is already used by the LD2410 or the status LED");

static std::atomic<bool> s_matter_started{false};
static uint16_t s_occupancy_endpoint_id;

// ---------------------------------------------------------------------------
// Radar -> occupancy
//
// The radar poll (esp_timer task) hands every presence change to the Matter
// thread, in order, so a short detection still marks the room occupied. It
// also checks that the cluster really took the last value: when a report
// could not be queued or the cluster was not ready, it is sent again, so the
// state seen by controllers cannot stay wrong until the next change.

static std::atomic<int> s_reports_queued{0};    // reports queued for the Matter thread, not run yet
static std::atomic<int> s_occupancy_applied{-1}; // last value the cluster took (0/1), -1 = none

// Runs on the Matter thread. The cluster applies HoldTime itself: occupied is
// reported at once, unoccupied only after HoldTime seconds without presence.
static void report_occupancy(intptr_t present)
{
    OccupancySensingCluster *cluster = OccupancySensing::FindClusterOnEndpoint(s_occupancy_endpoint_id);
    if (cluster) {
        cluster->SetOccupancy(present != 0);
        s_occupancy_applied = present ? 1 : 0;
    } else {
        s_occupancy_applied = -1;
    }
    s_reports_queued--;
}

static bool queue_occupancy_report(bool present)
{
    s_reports_queued++;
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(report_occupancy, present) != CHIP_NO_ERROR) {
        s_reports_queued--;
        return false;
    }
    return true;
}

static void radar_poll_cb(void *arg)
{
    static bool s_radar_ok = true;
    static bool s_present = false;
    static int s_queued_value = -1; // last value handed to the Matter thread

    ld2410_target_t t = {};
    const bool has_frame = ld2410_get_target(&t);
    const bool fresh = presence_radar_fresh(has_frame, t.timestamp_ms, esp_timer_get_time() / 1000,
                                            CONFIG_RADAR_TIMEOUT_SEC * 1000LL);
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
    const bool present = has_frame && fresh && presence_detected(&t.report, CONFIG_PRESENCE_MAX_DISTANCE_CM);
    if (present != s_present) {
        s_present = present;
        ESP_LOGI(TAG, "Radar: %s (moving %s %u cm, still %s %u cm)", present ? "presence" : "no one",
                 t.report.moving ? "yes" : "no", t.report.moving_cm, t.report.still ? "yes" : "no",
                 t.report.still_cm);
        status_led_set_presence(present);
    }

    if (!s_matter_started) {
        return;
    }
    if (s_queued_value != (int)present) {
        // Each change is reported once: every "unoccupied" restarts the hold timer.
        if (queue_occupancy_report(present)) {
            s_queued_value = present;
        }
    } else if (s_reports_queued == 0 && s_occupancy_applied != s_queued_value) {
        // Not taken (cluster not ready): send it again, at most once a second.
        static int s_retry_polls;
        if (++s_retry_polls >= 10) {
            s_retry_polls = 0;
            ESP_LOGW(TAG, "Occupancy not applied, sending it again");
            queue_occupancy_report(present);
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

    // HoldTime (and HoldTimeLimits, 1..300 s, default 30 s) is handled and
    // persisted by the cluster; Home Assistant shows it as a setting.
    cluster_t *cluster = cluster::get(endpoint, OccupancySensing::Id);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time(cluster, 30) != nullptr, nullptr);
    VerifyOrReturnValue(cluster::occupancy_sensing::attribute::create_hold_time_limits(cluster, NULL, 0, 0) != nullptr,
                        nullptr);
    return endpoint;
}

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

// Runs on the Matter thread. Slow blink only while the device can really be
// added: the pairing window closes after CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS
// (15 min) at boot, or 5 min after the last controller removed the device.
static void update_led_mode()
{
    const bool pairing = !is_commissioned() &&
                         chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    status_led_set_mode(pairing ? STATUS_LED_COMMISSIONING : STATUS_LED_PRESENCE);
}

static void print_onboarding_codes_if_necessary()
{
    VerifyOrReturn(!is_commissioned());
    // No BLE on ESP32-S2: the controller finds the device on the local network.
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(!is_commissioned());

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
        // The Wi-Fi driver resets the TX power limit whenever it starts.
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
        diag_log_heap(); // the lowest free RAM is reached while commissioning
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Pairing window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        if (!is_commissioned()) {
            ESP_LOGW(TAG, "Pairing window closed: restart the device to add it to a controller");
        }
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

    // The TX power limit is applied on WIFI_EVENT_STA_START (app_event_cb).
    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
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

// While Wi-Fi is not configured, before the radio starts: the LED blinks the
// previous reset reason (1 normal, 2 brownout, 3 crash, 4 watchdog) and the
// log repeats the reset history, so a board that resets as soon as its radio
// transmits can still be diagnosed without tools.
static void show_last_reset()
{
    const int seconds = CONFIG_DIAG_BOOT_WINDOW_SEC;
    if (seconds <= 0) {
        return;
    }
    status_led_set_code(diag_last_reset_blinks());
    for (int left = seconds; left > 0; left -= 2) {
        char report[512];
        diag_get_report(report, sizeof(report));
        ESP_LOGW(TAG, "Wi-Fi setup starts in %d s. %s", left, report);
        vTaskDelay(pdMS_TO_TICKS(left >= 2 ? 2000 : 1000));
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

    err = status_led_init(CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_ACTIVE_HIGH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Status LED not available, err:%d", err);
    }
    if (!wifi_setup_is_provisioned()) {
        show_last_reset();
        // Nothing to reset yet: the button is only enabled once Wi-Fi is set.
        status_led_set_mode(STATUS_LED_WIFI_SETUP);
        wifi_setup_run_portal(); // restarts once Wi-Fi is configured
    }
    err = reset_button_init(CONFIG_RESET_BUTTON_GPIO, kFactoryResetHoldMs, factory_reset_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reset button not available, err:%d", err);
    }
    status_led_set_mode(STATUS_LED_COMMISSIONING);

    err = ld2410_init(CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the LD2410 driver, err:%d", err);
    }

    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running");
    }
    diag_log_heap();
    diag_start_heap_log(kHeapLogPeriodSec);

    static esp_timer_handle_t s_radar_timer;
    const esp_timer_create_args_t timer_args = {
        .callback = radar_poll_cb,
        .name = "radar_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_radar_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_radar_timer, kRadarPollUs));
}
