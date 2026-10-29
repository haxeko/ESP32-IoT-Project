/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter over Wi-Fi temperature sensor on an ESP32-C3 SuperMini with the "ENS160 + AHT21" module (I2C). The AHT21
// of the module measures the temperature and the humidity; the ENS160 measures the air quality (TVOC, eCO2, AQI-UBA)
// and has no temperature sensor: it uses the values of the AHT21 to compensate its readings.
//
// The phone sends the Wi-Fi SSID and password over BLE during commissioning; afterwards the device talks to the
// controllers directly over the Wi-Fi LAN and BLE is released.
//
// Endpoints (their numbers follow the creation order and must not change: controllers that paired with an earlier
// firmware keep them across OTA updates):
//   0 Root Node: Basic Information (+ ProductLabel, SerialNumber), OTA Requestor, Wi-Fi network clusters
//   1 Temperature Sensor, 2 Humidity Sensor, 3 Air Quality Sensor (sensor_endpoints.cpp)

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <inttypes.h>
#include <nvs_flash.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <atomic>

#include <drivers/status_led.h>
#include <drivers/user_button.h>

#include "board.h"
#include "device_info.h"
#include "sensor_endpoints.h"
#include "sensors.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace chip::app::Clusters;

static constexpr uint32_t kFactoryResetHoldMs = 5000;
// Pairing mode opened by a double click, as long as the window opened at boot when not commissioned (15 min)
static constexpr uint32_t kPairingWindowSec = CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS;
// Window reopened after the last fabric is removed
static constexpr uint32_t kRecommissioningWindowSec = 300;

// Kconfig leaves a bool option undefined when it is off.
#if CONFIG_STATUS_LED_ACTIVE_HIGH
static constexpr bool kLedActiveHigh = true;
#else
static constexpr bool kLedActiveHigh = false;
#endif

static std::atomic<bool> s_matter_started{false};
// Set on the Matter task once BLE has been shut down after commissioning (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING); it
// stays off until the next reboot.
static bool s_ble_released;

// ---------------------------------------------------------------------------------------------------------------------
// Sensors

// Sensor task, after every reading. The sensors run before and without Matter: the values are in the log before
// commissioning too.
static void sensor_values_cb(const sensor_values_t *values)
{
    status_led_set_sensor_error(!values->climate_valid || !values->air_found);
    if (s_matter_started) {
        sensor_endpoints_update(values);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Commissioning

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

// Matter task. The LED blinks while the commissioning window is open (waiting to be added to an app).
static void update_led()
{
    status_led_set_pairing(chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen());
}

// Matter stack lock held. QR code, its web link and manual pairing code of the setup code (SDK test code by default:
// passcode 20202021, discriminator 3840), for the windows opened with it (never for a window opened by a controller
// for Multi-Admin, which has its own code).
static void print_onboarding_codes()
{
    PrintOnboardingCodes(chip::RendezvousInformationFlags(s_ble_released ? chip::RendezvousInformationFlag::kOnNetwork
                                                                         : chip::RendezvousInformationFlag::kBLE));
}

// Matter stack lock held (Matter task).
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
    print_onboarding_codes();
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        if (!is_commissioned()) {
            print_onboarding_codes(); // the SDK opened the boot commissioning window (BLE and network)
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange:
        ESP_LOGI(TAG, "Wi-Fi connectivity changed");
        break;

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
        break;

    default:
        break;
    }
    update_led();
}

// How long the LED blinks for an Identify TriggerEffect command (0: stop).
static uint32_t identify_effect_ms(uint8_t effect_id)
{
    switch (static_cast<Identify::EffectIdentifierEnum>(effect_id)) {
    case Identify::EffectIdentifierEnum::kBlink:
        return 1000;
    case Identify::EffectIdentifierEnum::kOkay:
        return 2000;
    case Identify::EffectIdentifierEnum::kChannelChange:
        return 8000;
    case Identify::EffectIdentifierEnum::kBreathe:
        return 15000;
    default: // kFinishEffect, kStopEffect
        return 0;
    }
}

// Identify cluster of the three sensor endpoints (IdentifyType "visible indicator"): the LED blinks fast.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify endpoint %u: type %u, effect %u, variant %u", endpoint_id, type, effect_id,
             effect_variant);
    switch (type) {
    case identification::START:
        status_led_set_identify(true);
        break;
    case identification::STOP:
        status_led_set_identify(false);
        break;
    case identification::EFFECT:
        status_led_identify_for(identify_effect_ms(effect_id));
        break;
    }
    return ESP_OK;
}

// A read-only sensor: no writable attribute of the application to handle.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

static esp_err_t start_matter()
{
    // Manufacturer / Model / Serial number shown by controllers
    device_info_init();

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    VerifyOrReturnError(node != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create Matter node"));
    device_info_add_attributes(node);

    esp_err_t err = sensor_endpoints_create(node);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to create the sensor endpoints"));

    // Wi-Fi: the station is started by the Matter stack with the credentials from commissioning
    err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
    // Device name "ENS160-B2C4": NodeLabel when none is stored yet, Bluetooth name
    device_info_apply_name();
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
        update_led();
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Pairing mode ignored, Matter task busy: %" CHIP_ERROR_FORMAT, err.Format());
    }
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
    ESP_LOGI(TAG, "Target %s, board %s, Matter over Wi-Fi; button GPIO%d, LED GPIO%d (-1: none)", CONFIG_IDF_TARGET,
             CONFIG_BOARD_NAME, CONFIG_RESET_BUTTON_GPIO, CONFIG_STATUS_LED_GPIO);
    // esp-matter logs every attribute write at info level, i.e. the TVOC and eCO2 values at most readings; the
    // sensor task logs all the values once a minute instead.
    esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);

    if (status_led_init(CONFIG_STATUS_LED_GPIO, kLedActiveHigh) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the status LED");
    }
    const user_button_config_t button_config = {
        .on_double_click = pairing_button_cb,
        .on_triple_click = nullptr,
        .on_quadruple_click = nullptr,
        .on_long_press = factory_reset_cb,
        .long_press_ms = kFactoryResetHoldMs,
    };
    if (user_button_init(CONFIG_RESET_BUTTON_GPIO, &button_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the button");
    }

    if (sensors_start(sensor_values_cb) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the sensors");
    }

    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running");
    }
}
