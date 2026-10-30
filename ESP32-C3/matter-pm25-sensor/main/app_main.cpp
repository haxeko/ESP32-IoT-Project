/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Air Quality Sensor (device type 0x002C) with a PM2.5 concentration
// measurement cluster, fed by a UART PM sensor, and a CO2 concentration
// measurement cluster fed by an I2C CO2 sensor (CONFIG_CO2_SENSOR_SCD4X). The
// OLED (with its on/off button) and the WS2812 indicator are handled locally
// (local_ui.cpp) and are not exposed to Matter. The main button
// (CONFIG_BUTTON_GPIO) resets the device, opens pairing mode and switches the
// screen.

#include <app/server/CommissioningWindowManager.h>
#include <app/InteractionModelEngine.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <atomic>
#include <iterator>
#include <math.h>

// drivers implemented by this example
#include <device_info.h>
#include <drivers/button.h>
#include <drivers/i2c_bus.h>
#include <drivers/pm_sensor.h>
#include <drivers/scd4x.h>
#include <local_ui.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Upper limit of the PMS5003 / SDS011 measuring range, in µg/m³.
static constexpr float kPmMaxMeasuredValue = 1000.0f;
// Upper limit of the SCD40 / SCD41 output range, in ppm.
static constexpr float kCo2MaxMeasuredValue = 40000.0f;

// Factory reset by powering the device on kFactoryResetBootCount times in a
// row, each time for less than kFactoryResetWindowUs...
static constexpr uint8_t kFactoryResetBootCount = 5;
static constexpr uint64_t kFactoryResetWindowUs = 10 * 1000 * 1000;
static constexpr char kNvsNamespace[] = "app";
static constexpr char kNvsBootCountKey[] = "boot_count";

// ...or with the main button (CONFIG_BUTTON_GPIO): hold it for
// kResetButtonHoldMs, then release it. The OLED counts down from
// kResetButtonShowMs on, so a short press shows nothing; releasing the button
// before the end cancels, and a button stuck down never resets.
static constexpr uint32_t kResetButtonShowMs = 1000;
static constexpr uint32_t kResetButtonHoldMs = 5000;
// Main button pressed twice: pairing mode (commissioning window) for this
// long. Pressed 4 times: PM2.5 alone <-> PM2.5 + CO2 on the OLED.
static constexpr uint16_t kPairingWindowSeconds = 300;
static constexpr int kPairingClicks = 2;
static constexpr int kLayoutClicks = 4;

#if CONFIG_BUTTON_GPIO >= 0
static_assert(CONFIG_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_RX_PIN &&
              CONFIG_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_TX_PIN &&
              CONFIG_BUTTON_GPIO != CONFIG_OLED_SDA_PIN &&
              CONFIG_BUTTON_GPIO != CONFIG_OLED_SCL_PIN &&
              CONFIG_BUTTON_GPIO != CONFIG_STATUS_LED_GPIO &&
              CONFIG_BUTTON_GPIO != CONFIG_DISPLAY_BUTTON_GPIO,
              "The button GPIO is already used");
#if CONFIG_IDF_TARGET_ESP32C3
// Pressed during a reset, a strapping pin changes the boot mode.
static_assert(CONFIG_BUTTON_GPIO != 2 && CONFIG_BUTTON_GPIO != 8 && CONFIG_BUTTON_GPIO != 9 &&
              (CONFIG_BUTTON_GPIO < 11 || CONFIG_BUTTON_GPIO > 19),
              "Button: GPIO2/8/9 are strapping pins, GPIO11-17 flash, GPIO18-19 USB");
#endif
#endif

// Names of the esp_reset_reason_t values, in their order, for the boot log: a
// watchdog, panic or brown-out reset points at a problem to look into.
static const char *const k_reset_reasons[] = {
    "unknown", "power-on", "external pin", "software", "panic", "interrupt watchdog", "task watchdog",
    "other watchdog", "deep sleep", "brown-out", "SDIO", "USB", "JTAG", "eFuse error", "power glitch", "CPU lock-up",
};

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    return (size_t)reason < std::size(k_reset_reasons) ? k_reset_reasons[reason] : "?";
}

// Logs why the Wi-Fi connection was lost (wifi_err_reason_t: e.g. 2 = auth
// expired, 15 = 4-way handshake timeout, 200 = beacon timeout, 201 = AP not
// found, 205 = connection failed). The Matter stack logs the event without
// the reason, then reconnects by itself every 5 s. Runs in the system event task.
static void wifi_disconnected_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
    ESP_LOGW(TAG, "Wi-Fi disconnected: reason %u, RSSI %d", event->reason, event->rssi);
}

// Matter AirQuality value sent for each level of the OLED/LEDs, and how
// Apple Home shows it:
//   TỐT         -> Good          (HomeKit: Good)
//   KHÁ         -> Fair          (HomeKit: Fair)
//   KHÔNG TỐT   -> Moderate      (HomeKit: Inferior)
//   KÉM         -> Poor          (HomeKit: Poor)
//   CỰC KỲ KÉM  -> ExtremelyPoor (HomeKit: Poor)
// No value (NAN) -> Unknown.
static AirQuality::AirQualityEnum pm25_to_air_quality(float pm25)
{
    static constexpr AirQuality::AirQualityEnum k_level_to_matter[] = {
        AirQuality::AirQualityEnum::kGood,
        AirQuality::AirQualityEnum::kFair,
        AirQuality::AirQualityEnum::kModerate,
        AirQuality::AirQualityEnum::kPoor,
        AirQuality::AirQualityEnum::kExtremelyPoor,
    };
    const int level = local_ui_pm25_level(pm25);
    if (level < 0 || level >= (int)std::size(k_level_to_matter)) {
        return AirQuality::AirQualityEnum::kUnknown;
    }
    return k_level_to_matter[level];
}

// Must be called from the Matter thread.
static void update_measured_value(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, float value)
{
    nullable<float> measured = isnan(value) ? nullable<float>() : nullable<float>(value);
    esp_matter_attr_val_t val = esp_matter_nullable_float(measured);
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

// Set once the Matter stack is running. Until then (or if Matter fails to
// start) the sensor, OLED and LEDs keep working locally.
static std::atomic<bool> s_matter_started{false};
static uint16_t s_air_quality_endpoint_id;
static std::atomic<bool> s_factory_reset_started{false};

// esp_matter::factory_reset() only schedules the reset on the Matter thread
// (which takes a few seconds). If the device has not restarted after this
// long, the reset was lost (e.g. Matter event queue full): erase NVS directly.
static constexpr uint64_t kFactoryResetFallbackUs = 20 * 1000 * 1000;

static void erase_nvs_and_restart()
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
    esp_restart();
}

static void factory_reset_fallback_cb(void *arg)
{
    ESP_LOGE(TAG, "Factory reset did not complete, erasing NVS");
    erase_nvs_and_restart();
}

// Erases the Wi-Fi credentials and the Matter fabrics, then restarts. Can be
// called from any task; only the first call does something.
static void start_factory_reset(const char *reason)
{
    if (s_factory_reset_started.exchange(true)) {
        return;
    }
    ESP_LOGW(TAG, "Factory reset requested by %s", reason);
    local_ui_set_reset(LOCAL_UI_RESET_RUNNING, 0);
    if (s_matter_started) {
        static esp_timer_handle_t s_fallback_timer;
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = factory_reset_fallback_cb;
        timer_args.name = "factory_reset";
        if (esp_timer_create(&timer_args, &s_fallback_timer) == ESP_OK) {
            esp_timer_start_once(s_fallback_timer, kFactoryResetFallbackUs);
        }
        esp_matter::factory_reset();    // restarts the device when done
        return;
    }
    // esp_matter::factory_reset() needs a running Matter stack, and a broken
    // Matter storage may be why it is not running: erase it directly.
    erase_nvs_and_restart();
}

#if CONFIG_BUTTON_GPIO >= 0
// Pairing mode: shows the manual pairing code of the device on the OLED.
// Runs on the Matter thread.
static void show_pairing_code()
{
    char code[chip::kManualSetupLongCodeCharLength + 2];
    chip::MutableCharSpan span(code);
    if (GetManualPairingCode(span, chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork)) ==
        CHIP_NO_ERROR) {
        code[span.size()] = '\0';
    } else {
        code[0] = '\0';    // the OLED then only shows the pairing mode
    }
    local_ui_set_pairing(code);
}

// Opens a commissioning window, so that the device can be paired (again, or
// with one more controller) with its pairing code. Runs on the Matter thread.
static void open_pairing_window()
{
    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissionMgr.IsCommissioningWindowOpen()) {
        // A controller opened it with a code of its own (enhanced commissioning):
        // the code of the device does not work then, so show none.
        if (commissionMgr.CommissioningWindowStatusForCluster() ==
            AdministratorCommissioning::CommissioningWindowStatusEnum::kEnhancedWindowOpen) {
            local_ui_set_pairing("");
        } else {
            show_pairing_code();
        }
        ESP_LOGI(TAG, "Pairing mode: the commissioning window is already open");
        return;
    }
    // On Wi-Fi, controllers find the device on the network; otherwise it
    // needs BLE as well.
    const chip::CommissioningWindowAdvertisement advertisement =
        chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned()
            ? chip::CommissioningWindowAdvertisement::kDnssdOnly
            : chip::CommissioningWindowAdvertisement::kAllSupported;
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(kPairingWindowSeconds),
                                                                advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    ESP_LOGI(TAG, "Pairing mode for %u s", kPairingWindowSeconds);
    show_pairing_code();
}

// Runs in the esp_timer task.
static void button_cb(const button_event_t *event, void *user_data)
{
    if (s_factory_reset_started) {
        return;
    }
    switch (event->type) {
    case BUTTON_RELEASED:
        if (event->held_ms >= kResetButtonHoldMs) {
            start_factory_reset("the button");
        } else {
            local_ui_set_reset(LOCAL_UI_RESET_NONE, 0);    // released too early: cancelled
        }
        break;

    case BUTTON_HELD:
        if (event->held_ms >= kResetButtonHoldMs) {
            local_ui_set_reset(LOCAL_UI_RESET_RELEASE, 0);
        } else if (event->held_ms >= kResetButtonShowMs) {
            // Whole seconds left, rounded up: 4, 3, 2, 1.
            local_ui_set_reset(LOCAL_UI_RESET_HOLD, (int)((kResetButtonHoldMs - event->held_ms + 999) / 1000));
        }
        break;

    case BUTTON_CLICKS:
        ESP_LOGI(TAG, "Button pressed %d times", event->clicks);
        if (event->clicks == kPairingClicks) {
            if (!s_matter_started) {
                ESP_LOGW(TAG, "Matter is not running: no pairing mode");
                break;
            }
            CHIP_ERROR err = chip::DeviceLayer::SystemLayer().ScheduleLambda([]() { open_pairing_window(); });
            if (err != CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "Failed to schedule the pairing mode, err:%" CHIP_ERROR_FORMAT, err.Format());
            }
        } else if (event->clicks == kLayoutClicks) {
            local_ui_toggle_layout();
        }
        break;
    }
}
#endif

static void pm_sensor_notification(const pm_sensor_data_t *data, void *user_data)
{
    pm_sensor_data_t d = *data;

    if (d.valid) {
        local_ui_set_pm25(d.pm2_5);
    }
    if (!s_matter_started) {
        return;
    }
    const uint16_t endpoint_id = s_air_quality_endpoint_id;

    // schedule the attribute update so that we can report it from matter thread
    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, d]() {
        // MeasuredValue must stay within [MinMeasuredValue, MaxMeasuredValue].
        const float pm2_5 = d.valid ? fminf(fmaxf(d.pm2_5, 0.0f), kPmMaxMeasuredValue) : NAN;

        update_measured_value(endpoint_id, Pm25ConcentrationMeasurement::Id,
                              Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, pm2_5);

        esp_matter_attr_val_t val = esp_matter_enum8(chip::to_underlying(pm25_to_air_quality(pm2_5)));
        attribute::update(endpoint_id, AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &val);
    });
    if (err != CHIP_NO_ERROR) {
        // Matter event queue full: this value is lost, the next one is sent normally.
        ESP_LOGW(TAG, "Failed to schedule the Matter update, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

#if CONFIG_CO2_SENSOR_SCD4X
static void co2_sensor_notification(const scd4x_data_t *data, void *user_data)
{
    const float co2 = data->valid ? data->co2_ppm : NAN;
    local_ui_set_co2(co2);
    if (!s_matter_started) {
        return;
    }
    const uint16_t endpoint_id = s_air_quality_endpoint_id;

    // schedule the attribute update so that we can report it from matter thread
    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, co2]() {
        // MeasuredValue must stay within [MinMeasuredValue, MaxMeasuredValue].
        const float value = isnan(co2) ? NAN : fminf(fmaxf(co2, 0.0f), kCo2MaxMeasuredValue);
        update_measured_value(endpoint_id, CarbonDioxideConcentrationMeasurement::Id,
                              CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, value);
    });
    if (err != CHIP_NO_ERROR) {
        // Matter event queue full: this value is lost, the next one is sent normally.
        ESP_LOGW(TAG, "Failed to schedule the Matter update, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}
#endif

// Adds the Numeric Measurement feature (MeasuredValue from 0 to max_value, in
// unit) to a concentration measurement cluster.
template <typename NumericMeasurementConfig>
static esp_err_t add_numeric_measurement(cluster_t *cluster,
                                         esp_err_t (*add_feature)(cluster_t *, NumericMeasurementConfig *),
                                         float max_value, uint8_t unit)
{
    VerifyOrReturnError(cluster != nullptr, ESP_FAIL);
    NumericMeasurementConfig config;
    config.measured_value = nullable<float>();
    config.min_measured_value = nullable<float>(0.0f);
    config.max_measured_value = nullable<float>(max_value);
    config.measurement_unit = unit;
    return add_feature(cluster, &config);
}

static endpoint_t *create_air_quality_sensor(node_t *node)
{
    air_quality_sensor::config_t air_quality_sensor_config;
    endpoint_t *endpoint = air_quality_sensor::create(node, &air_quality_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    VerifyOrReturnValue(endpoint != nullptr, nullptr);

    // AirQuality: enable all optional levels so that every AirQualityEnum value is allowed.
    // These features only set FeatureMap bits, so set them directly: esp-matter v1.4
    // declares the helpers as mod/vpoor/xpoor but defines them as moderate/very_poor/
    // extremely_poor, so they do not link.
    cluster_t *air_quality_cluster = cluster::get(endpoint, AirQuality::Id);
    attribute_t *feature_map = attribute::get(air_quality_cluster, Globals::Attributes::FeatureMap::Id);
    VerifyOrReturnValue(feature_map != nullptr, nullptr);
    esp_matter_attr_val_t feature_map_val = esp_matter_invalid(NULL);
    attribute::get_val(feature_map, &feature_map_val);
    feature_map_val.val.u32 |= chip::to_underlying(AirQuality::Feature::kFair) |
                               chip::to_underlying(AirQuality::Feature::kModerate) |
                               chip::to_underlying(AirQuality::Feature::kVeryPoor) |
                               chip::to_underlying(AirQuality::Feature::kExtremelyPoor);
    attribute::set_val(feature_map, &feature_map_val);

    // PM2.5 concentration measurement, measurement medium defaults to Air.
    cluster::pm25_concentration_measurement::config_t pm25_config;
    cluster_t *pm25_cluster = cluster::pm25_concentration_measurement::create(endpoint, &pm25_config,
                                                                              CLUSTER_FLAG_SERVER);
    VerifyOrReturnValue(add_numeric_measurement(pm25_cluster,
                        cluster::pm25_concentration_measurement::feature::numeric_measurement::add,
                        kPmMaxMeasuredValue,
                        chip::to_underlying(Pm25ConcentrationMeasurement::MeasurementUnitEnum::kUgm3)) == ESP_OK,
                        nullptr);

#if CONFIG_CO2_SENSOR_SCD4X
    // CO2 concentration measurement in ppm, measurement medium defaults to Air.
    cluster::carbon_dioxide_concentration_measurement::config_t co2_config;
    cluster_t *co2_cluster = cluster::carbon_dioxide_concentration_measurement::create(endpoint, &co2_config,
                                                                                       CLUSTER_FLAG_SERVER);
    VerifyOrReturnValue(add_numeric_measurement(co2_cluster,
                        cluster::carbon_dioxide_concentration_measurement::feature::numeric_measurement::add,
                        kCo2MaxMeasuredValue,
                        chip::to_underlying(CarbonDioxideConcentrationMeasurement::MeasurementUnitEnum::kPpm)) ==
                        ESP_OK, nullptr);
#endif

    return endpoint;
}

static void save_boot_count(uint8_t count)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, kNvsBootCountKey, count);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

// Counts quick power cycles. Returns true when a factory reset was requested.
// Only power-on resets count, so crashes, OTA updates or flashing never
// trigger a factory reset, and they clear the count. A brown-out (a quick
// unplug/replug can cause one) neither counts nor clears it.
static bool check_factory_reset_boot_count()
{
    const esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT) {
        save_boot_count(0);
        return false;
    }
    uint8_t count = 0;
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, kNvsBootCountKey, &count);
        nvs_close(handle);
    }
    if (reason == ESP_RST_POWERON && count < kFactoryResetBootCount) {
        count++;
        ESP_LOGI(TAG, "Quick power cycle %u/%u", count, kFactoryResetBootCount);
        save_boot_count(count);
    }
    if (count >= kFactoryResetBootCount) {
        // The count stays until the reset is done: it ends with a software
        // restart, which clears it, and a power cut before that retries it.
        return true;
    }

    // Staying powered on long enough clears the counter.
    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = [](void *) { save_boot_count(0); },
        .name = "boot_count",
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, kFactoryResetWindowUs);
    }
    return false;
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(commissionMgr.IsCommissioningWindowOpen() == false);

    // After removing last fabric, this example does not remove the Wi-Fi credentials
    // and still has IP connectivity so, only advertising on DNS-SD.
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

// "Connected" = online on Wi-Fi with at least one controller (Apple Home,
// Home Assistant...) subscribed to the device. Controllers keep a
// subscription open while they are reachable; when the last one goes away
// its subscription is dropped (at the latest after the negotiated max
// interval) and the icon disappears. Runs on the Matter thread.
static void update_matter_status()
{
    const bool online = chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected();
    const uint32_t subscriptions = chip::app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
        chip::app::ReadHandler::InteractionType::Subscribe);
    local_ui_set_matter_connected(online && subscriptions > 0);
}

// Task watchdog user for the Matter thread (CONFIG_ESP_TASK_WDT_PANIC): if the
// thread stops running the lambda below, the device restarts instead of
// staying off Matter while the local UI still looks alive.
static esp_task_wdt_user_handle_t s_matter_wdt;

// Subscriptions start and end without a device event, so also poll.
static void matter_status_timer_cb(void *arg)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        update_matter_status();
        if (s_matter_wdt != NULL) {
            esp_task_wdt_reset_user(s_matter_wdt);
        }
    });
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        // Paired, or the time ran out: end of the pairing mode on the OLED.
        ESP_LOGI(TAG, "Commissioning window closed");
        local_ui_set_pairing(NULL);
        break;

    default:
        break;
    }

    // Wi-Fi up/down, commissioning, fabric removal... all may change it.
    update_matter_status();
}

// How long the LEDs blink for an Identify TriggerEffect command (0 = stop).
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
    default:    // kFinishEffect, kStopEffect
        return 0;
    }
}

// Identify cluster (IdentifyType "visible indicator", set by esp-matter for this
// device type): the LEDs blink while a controller identifies the device.
// Runs on the Matter thread.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    switch (type) {
    case identification::callback_type_t::START:
        local_ui_identify(LOCAL_UI_IDENTIFY_FOREVER);    // until STOP
        break;
    case identification::callback_type_t::STOP:
        local_ui_identify(0);
        break;
    case identification::callback_type_t::EFFECT:
        local_ui_identify(identify_effect_ms(effect_id));
        break;
    }
    return ESP_OK;
}

// This is a read-only sensor, there are no writable attributes to handle.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

static esp_err_t start_matter()
{
    /* Manufacturer / Model shown by controllers */
    device_info_init();

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    device_info_set_name(&node_config);
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    VerifyOrReturnError(node != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create Matter node"));
    device_info_add_attributes(node);

    endpoint_t *air_quality_ep = create_air_quality_sensor(node);
    VerifyOrReturnError(air_quality_ep != nullptr, ESP_FAIL,
                        ESP_LOGE(TAG, "Failed to create air_quality_sensor endpoint"));
    s_air_quality_endpoint_id = endpoint::get_id(air_quality_ep);
    ESP_LOGI(TAG, "Air quality sensor created with endpoint_id %d", s_air_quality_endpoint_id);

    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;

    // The default event loop exists once Wi-Fi is initialized, by esp_matter::start().
    if (esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_disconnected_cb, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi disconnect reasons will not be logged");
    }

    static esp_timer_handle_t s_status_timer;
    const esp_timer_create_args_t timer_args = {
        .callback = matter_status_timer_cb,
        .name = "matter_status",
    };
    if (esp_timer_create(&timer_args, &s_status_timer) == ESP_OK) {
        // The timer feeds the Matter thread's watchdog: only watch it when the timer runs.
        if (esp_task_wdt_add_user("matter", &s_matter_wdt) != ESP_OK) {
            s_matter_wdt = NULL;    // task watchdog disabled in menuconfig
        }
        if (esp_timer_start_periodic(s_status_timer, 5 * 1000 * 1000) != ESP_OK && s_matter_wdt != NULL) {
            esp_task_wdt_delete_user(s_matter_wdt);
            s_matter_wdt = NULL;
        }
    }
    return ESP_OK;
}

// Initializes the default NVS partition. A partition without free pages, or
// written by a newer NVS format, can only be erased: otherwise Matter could
// never start (and could not be factory reset either) until the device is
// reflashed. Erasing it is a factory reset: fabrics and Wi-Fi are forgotten.
static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s), erasing it", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
}

extern "C" void app_main()
{
    init_nvs();

    ESP_LOGI(TAG, "%s %s, firmware %s", CONFIG_DEVICE_VENDOR_NAME, CONFIG_DEVICE_PRODUCT_NAME,
             esp_app_get_description()->version);
    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(esp_reset_reason()), esp_reset_reason());

    const bool factory_reset_requested = check_factory_reset_boot_count();

    /* The sensors, OLED and WS2812 work locally, before and without Matter */
    // I2C bus shared by the OLED and the CO2 sensor; on failure both are left out.
    i2c_bus_init(CONFIG_OLED_SDA_PIN, CONFIG_OLED_SCL_PIN);
    esp_err_t err = local_ui_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start local UI: %s", esp_err_to_name(err));
    }

    pm_sensor_config_t pm_config = {
        .rx_pin = CONFIG_PM_SENSOR_UART_RX_PIN,
        .tx_pin = CONFIG_PM_SENSOR_UART_TX_PIN,
        .report_interval_ms = CONFIG_PM_SENSOR_REPORT_INTERVAL_SEC * 1000,
        .cb = pm_sensor_notification,
        .user_data = NULL,
    };
    err = pm_sensor_init(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PM sensor driver: %s", esp_err_to_name(err));
    }

#if CONFIG_CO2_SENSOR_SCD4X
    scd4x_config_t co2_config = {
        .bus = i2c_bus_get(),
        .report_interval_ms = CONFIG_CO2_SENSOR_REPORT_INTERVAL_SEC * 1000,
        .cb = co2_sensor_notification,
        .user_data = NULL,
    };
    err = scd4x_init(&co2_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CO2 sensor driver: %s", esp_err_to_name(err));
    }
#endif

    /* Matter is optional for the local functions: on failure, keep running without it */
    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running; the device keeps working locally");
    }

    /* Factory reset, once Matter has started or failed to */
    if (factory_reset_requested) {
        start_factory_reset("quick power cycles");
    }
#if CONFIG_BUTTON_GPIO >= 0
    err = button_init(CONFIG_BUTTON_GPIO, button_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the button: %s", esp_err_to_name(err));
    }
#endif
}
