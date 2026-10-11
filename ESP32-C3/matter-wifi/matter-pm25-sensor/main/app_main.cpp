/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter Air Quality Sensor (device type 0x002C) with PM1.0 / PM2.5 / PM10
// concentration measurement clusters, fed by a UART PM sensor.

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <bsp/esp-bsp.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <nvs_flash.h>

#include <app_reset.h>
#include <common_macros.h>

#include <math.h>

// drivers implemented by this example
#include <drivers/pm_sensor.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Upper limit of the PMS5003 / SDS011 measuring range, in µg/m³.
static constexpr float kPmMaxMeasuredValue = 1000.0f;

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

static void pm_sensor_notification(const pm_sensor_data_t *data, void *user_data)
{
    uint16_t endpoint_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user_data));
    pm_sensor_data_t d = *data;

    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, d]() {
        float pm1_0 = d.valid ? d.pm1_0 : NAN;
        float pm2_5 = d.valid ? d.pm2_5 : NAN;
        float pm10 = d.valid ? d.pm10 : NAN;

        update_measured_value(endpoint_id, Pm25ConcentrationMeasurement::Id,
                              Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, pm2_5);
        update_measured_value(endpoint_id, Pm10ConcentrationMeasurement::Id,
                              Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, pm10);
#if CONFIG_PM_SENSOR_MODEL_PMS
        update_measured_value(endpoint_id, Pm1ConcentrationMeasurement::Id,
                              Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, pm1_0);
#else
        (void)pm1_0;
#endif

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

    // Concentration measurement clusters, measurement medium defaults to Air.
    cluster::pm25_concentration_measurement::config_t pm25_config;
    cluster_t *pm25_cluster = cluster::pm25_concentration_measurement::create(endpoint, &pm25_config,
                                                                              CLUSTER_FLAG_SERVER);
    VerifyOrReturnValue(add_numeric_measurement(pm25_cluster,
                        cluster::pm25_concentration_measurement::feature::numeric_measurement::add) == ESP_OK,
                        nullptr);

    cluster::pm10_concentration_measurement::config_t pm10_config;
    cluster_t *pm10_cluster = cluster::pm10_concentration_measurement::create(endpoint, &pm10_config,
                                                                              CLUSTER_FLAG_SERVER);
    VerifyOrReturnValue(add_numeric_measurement(pm10_cluster,
                        cluster::pm10_concentration_measurement::feature::numeric_measurement::add) == ESP_OK,
                        nullptr);

#if CONFIG_PM_SENSOR_MODEL_PMS
    cluster::pm1_concentration_measurement::config_t pm1_config;
    cluster_t *pm1_cluster = cluster::pm1_concentration_measurement::create(endpoint, &pm1_config,
                                                                            CLUSTER_FLAG_SERVER);
    VerifyOrReturnValue(add_numeric_measurement(pm1_cluster,
                        cluster::pm1_concentration_measurement::feature::numeric_measurement::add) == ESP_OK,
                        nullptr);
#endif

    return endpoint;
}

static esp_err_t factory_reset_button_register()
{
    button_handle_t push_button;
    esp_err_t err = bsp_iot_button_create(&push_button, NULL, BSP_BUTTON_NUM);
    VerifyOrReturnError(err == ESP_OK, err);
    return app_reset_button_register(push_button);
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

extern "C" void app_main()
{
    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize push button on the dev-kit to reset the device */
    esp_err_t err = factory_reset_button_register();
    ABORT_APP_ON_FAILURE(ESP_OK == err, ESP_LOGE(TAG, "Failed to initialize reset button, err:%d", err));

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    endpoint_t *air_quality_ep = create_air_quality_sensor(node);
    ABORT_APP_ON_FAILURE(air_quality_ep != nullptr, ESP_LOGE(TAG, "Failed to create air_quality_sensor endpoint"));
    uint16_t air_quality_endpoint_id = endpoint::get_id(air_quality_ep);
    ESP_LOGI(TAG, "Air quality sensor created with endpoint_id %d", air_quality_endpoint_id);

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Start reading the PM sensor once the Matter stack is running */
    pm_sensor_config_t pm_config = {
        .rx_pin = CONFIG_PM_SENSOR_UART_RX_PIN,
        .tx_pin = CONFIG_PM_SENSOR_UART_TX_PIN,
        .report_interval_ms = CONFIG_PM_SENSOR_REPORT_INTERVAL_SEC * 1000,
        .cb = pm_sensor_notification,
        .user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(air_quality_endpoint_id)),
    };
    err = pm_sensor_init(&pm_config);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialize PM sensor driver, err:%d", err));
}
