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
#include <app/server/Server.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>


#include <atomic>
#include <math.h>

// drivers implemented by this example
#include <device_info.h>
#include <drivers/pm_sensor.h>
#include <local_ui.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Upper limit of the PMS5003 / SDS011 measuring range, in µg/m³.
static constexpr float kPmMaxMeasuredValue = 1000.0f;

// GPIO9 (BOOT button) is the OLED SCL line, so factory reset is done by
// powering the device on kFactoryResetBootCount times in a row, each time for
// less than kFactoryResetWindowUs.
static constexpr uint8_t kFactoryResetBootCount = 5;
static constexpr uint64_t kFactoryResetWindowUs = 10 * 1000 * 1000;
static constexpr char kNvsNamespace[] = "app";
static constexpr char kNvsBootCountKey[] = "boot_count";

// Maps PM2.5 (µg/m³) to the Matter AirQuality enum using the US EPA 2024 AQI
// breakpoints. Apple Home shows the AirQuality value as the sensor state.
static AirQuality::AirQualityEnum pm25_to_air_quality(float pm25)
{
    if (pm25 <= 9.0f) {
        return AirQuality::AirQualityEnum::kGood;           // AQI 0-50
    } else if (pm25 <= 35.4f) {
        return AirQuality::AirQualityEnum::kFair;           // AQI 51-100
    } else if (pm25 <= 55.4f) {
        return AirQuality::AirQualityEnum::kModerate;       // AQI 101-150
    } else if (pm25 <= 125.4f) {
        return AirQuality::AirQualityEnum::kPoor;           // AQI 151-200
    } else if (pm25 <= 225.4f) {
        return AirQuality::AirQualityEnum::kVeryPoor;       // AQI 201-300
    }
    return AirQuality::AirQualityEnum::kExtremelyPoor;      // AQI 301+
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
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, d]() {
        float pm2_5 = d.valid ? d.pm2_5 : NAN;

        update_measured_value(endpoint_id, Pm25ConcentrationMeasurement::Id,
                              Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, pm2_5);

        AirQuality::AirQualityEnum air_quality = d.valid ? pm25_to_air_quality(pm2_5)
                                                         : AirQuality::AirQualityEnum::kUnknown;
        esp_matter_attr_val_t val = esp_matter_enum8(chip::to_underlying(air_quality));
        attribute::update(endpoint_id, AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &val);
    });
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
    // These features only set FeatureMap bits, so set them directly (the feature helper
    // names differ between esp-matter releases).
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
// Only real power-on resets count, so crashes, OTA updates or flashing never
// trigger a factory reset.
static bool check_factory_reset_boot_count()
{
    if (esp_reset_reason() != ESP_RST_POWERON) {
        save_boot_count(0);
        return false;
    }
    uint8_t count = 0;
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, kNvsBootCountKey, &count);
        nvs_close(handle);
    }
    count++;
    ESP_LOGI(TAG, "Quick power cycle %u/%u", count, kFactoryResetBootCount);
    if (count >= kFactoryResetBootCount) {
        save_boot_count(0);
        return true;
    }
    save_boot_count(count);

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

// "Connected" = commissioned to at least one fabric and online on Wi-Fi.
// Runs on the Matter thread, from the device event callback.
static void update_matter_status()
{
    const bool commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    const bool online = chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected();
    local_ui_set_matter_connected(commissioned && online);
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

    default:
        break;
    }

    // Wi-Fi up/down, commissioning, fabric removal... all may change it.
    update_matter_status();
}

// This callback is invoked when clients interact with the Identify Cluster.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
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
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    VerifyOrReturnError(node != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create Matter node"));

    endpoint_t *air_quality_ep = create_air_quality_sensor(node);
    VerifyOrReturnError(air_quality_ep != nullptr, ESP_FAIL,
                        ESP_LOGE(TAG, "Failed to create air_quality_sensor endpoint"));
    s_air_quality_endpoint_id = endpoint::get_id(air_quality_ep);
    ESP_LOGI(TAG, "Air quality sensor created with endpoint_id %d", s_air_quality_endpoint_id);

    esp_err_t err = esp_matter::start(app_event_cb);
    VerifyOrReturnError(err == ESP_OK, err, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    s_matter_started = true;
    return ESP_OK;
}

extern "C" void app_main()
{
    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    ESP_LOGI(TAG, "%s %s, firmware %s", CONFIG_DEVICE_VENDOR_NAME, CONFIG_DEVICE_PRODUCT_NAME,
             esp_app_get_description()->version);

    const bool factory_reset_requested = check_factory_reset_boot_count();

    /* The sensor, OLED, WS2812 and LD2410 work locally, before and without Matter */
    esp_err_t err = local_ui_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start local UI, err:%d", err);
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
        ESP_LOGE(TAG, "Failed to initialize PM sensor driver, err:%d", err);
    }

    /* Matter is optional for the local functions: on failure, keep running without it */
    if (start_matter() != ESP_OK) {
        ESP_LOGE(TAG, "Matter is not running; the device keeps working locally");
        return;
    }

    if (factory_reset_requested) {
        ESP_LOGW(TAG, "Factory reset requested by %u quick power cycles", kFactoryResetBootCount);
        esp_matter::factory_reset();
    }
}
