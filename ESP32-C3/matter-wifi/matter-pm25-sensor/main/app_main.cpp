/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Air Quality Sensor (device type 0x002C) with a PM2.5 concentration
// measurement cluster, fed by a UART PM sensor. The OLED, WS2812 indicator and
// LD2410 radar are handled locally (local_ui.cpp) and are not exposed to Matter.

#include <app/server/CommissioningWindowManager.h>
#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <atomic>
#include <iterator>
#include <math.h>

// drivers implemented by this example
#include <device_info.h>
#include <drivers/button.h>
#include <drivers/pm_sensor.h>
#include <local_ui.h>
#include <wifi_power.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Upper limit of the PMS5003 / SDS011 measuring range, in µg/m³.
static constexpr float kPmMaxMeasuredValue = 1000.0f;

// GPIO9 (BOOT button) is the OLED SCL line, so factory reset is done by
// powering the device on kFactoryResetBootCount times in a row, each time for
// less than kFactoryResetWindowUs...
static constexpr uint8_t kFactoryResetBootCount = 5;
static constexpr uint64_t kFactoryResetWindowUs = 10 * 1000 * 1000;
static constexpr char kNvsNamespace[] = "app";
static constexpr char kNvsBootCountKey[] = "boot_count";

// ...or with a button on CONFIG_FACTORY_RESET_BUTTON_GPIO: hold it for
// kResetButtonHoldMs, then release it. The OLED counts down from
// kResetButtonShowMs on, so a short press shows nothing; releasing the button
// before the end cancels, and a button stuck down never resets.
static constexpr uint32_t kResetButtonShowMs = 1000;
static constexpr uint32_t kResetButtonHoldMs = 5000;

#if CONFIG_FACTORY_RESET_BUTTON_GPIO >= 0
static_assert(CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_RX_PIN &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_TX_PIN &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_OLED_SDA_PIN &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_OLED_SCL_PIN &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_STATUS_LED_GPIO &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN,
              "The factory reset button GPIO is already used");
#if CONFIG_IDF_TARGET_ESP32C3
// Pressed during a reset, a strapping pin changes the boot mode.
static_assert(CONFIG_FACTORY_RESET_BUTTON_GPIO != 2 && CONFIG_FACTORY_RESET_BUTTON_GPIO != 8 &&
              CONFIG_FACTORY_RESET_BUTTON_GPIO != 9 &&
              (CONFIG_FACTORY_RESET_BUTTON_GPIO < 11 || CONFIG_FACTORY_RESET_BUTTON_GPIO > 19),
              "Factory reset button: GPIO2/8/9 are strapping pins, GPIO11-17 flash, GPIO18-19 USB");
#endif
#endif

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
// start) the sensor, OLED, LEDs and radar keep working locally.
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

#if CONFIG_FACTORY_RESET_BUTTON_GPIO >= 0
// Runs in the esp_timer task.
static void reset_button_cb(uint32_t held_ms, bool released, void *user_data)
{
    if (s_factory_reset_started) {
        return;
    }
    if (released) {
        if (held_ms >= kResetButtonHoldMs) {
            start_factory_reset("the button");
        } else {
            local_ui_set_reset(LOCAL_UI_RESET_NONE, 0);    // released too early: cancelled
        }
    } else if (held_ms >= kResetButtonHoldMs) {
        local_ui_set_reset(LOCAL_UI_RESET_RELEASE, 0);
    } else if (held_ms >= kResetButtonShowMs) {
        // Whole seconds left, rounded up: 4, 3, 2, 1.
        local_ui_set_reset(LOCAL_UI_RESET_HOLD, (int)((kResetButtonHoldMs - held_ms + 999) / 1000));
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

// Adds the Numeric Measurement feature (MeasuredValue in µg/m³) to a concentration measurement cluster.
template <typename NumericMeasurementConfig>
static esp_err_t add_numeric_measurement(cluster_t *cluster,
                                         esp_err_t (*add_feature)(cluster_t *, NumericMeasurementConfig *))
{
    VerifyOrReturnError(cluster != nullptr, ESP_FAIL);
    NumericMeasurementConfig config;
    config.measured_value = nullable<float>();
    config.min_measured_value = nullable<float>(0.0f);
    config.max_measured_value = nullable<float>(kPmMaxMeasuredValue);
    config.measurement_unit = chip::to_underlying(Pm25ConcentrationMeasurement::MeasurementUnitEnum::kUgm3);
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
                        cluster::pm25_concentration_measurement::feature::numeric_measurement::add) == ESP_OK,
                        nullptr);

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
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        wifi_power_update();
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
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        wifi_power_update(); /* Bluetooth off: Wi-Fi no longer needs modem sleep */
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

    const bool factory_reset_requested = check_factory_reset_boot_count();

    /* The sensor, OLED, WS2812 and LD2410 work locally, before and without Matter */
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

    /* Matter is optional for the local functions: on failure, keep running without it */
    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running; the device keeps working locally");
    }

    /* Factory reset, once Matter has started or failed to */
    if (factory_reset_requested) {
        start_factory_reset("quick power cycles");
    }
#if CONFIG_FACTORY_RESET_BUTTON_GPIO >= 0
    err = button_init(CONFIG_FACTORY_RESET_BUTTON_GPIO, reset_button_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the factory reset button: %s", esp_err_to_name(err));
    }
#endif
}
