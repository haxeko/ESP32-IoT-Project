/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <app/ConcreteClusterPath.h>
#include <app/clusters/air-quality-server/AirQualityCluster.h>
#include <data_model_provider/clusters/relative_humidity_measurement/integration.h>
#include <data_model_provider/clusters/temperature_measurement/integration.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <inttypes.h>
#include <sdkconfig.h>

#include <drivers/sensor_math.h>

#include "sensor_endpoints.h"

static const char *TAG = "sensor_ep";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using chip::app::DataModel::MakeNullable;
using chip::app::DataModel::Nullable;
// The same enums and MeasuredValue ID in every Concentration Measurement cluster
namespace Tvoc = TotalVolatileOrganicCompoundsConcentrationMeasurement;
using MeasurementMediumEnum = Tvoc::MeasurementMediumEnum;
using MeasurementUnitEnum = Tvoc::MeasurementUnitEnum;
using ConcentrationCreate = cluster_t *(*)(endpoint_t *, cluster::concentration_measurement::config_t *, uint8_t);

// Measuring ranges (MinMeasuredValue, MaxMeasuredValue): AHT21 -40 to 120 °C and 0 to 100 %, ENS160 TVOC 0 to
// 65000 ppb and eCO2 400 to 65000 ppm. Values outside are kept within them.
static constexpr int16_t kTemperatureMin = -4000; // 0.01 °C
static constexpr int16_t kTemperatureMax = 12000;
static constexpr uint16_t kHumidityMin = 0; // 0.01 %
static constexpr uint16_t kHumidityMax = 10000;
static constexpr uint16_t kTvocMin = 0;
static constexpr uint16_t kTvocMax = 65000;
#if CONFIG_SENSOR_ECO2_AS_CO2
static constexpr uint16_t kEco2Min = 400;
static constexpr uint16_t kEco2Max = 65000;
#endif

// Changes smaller than these are not written: they are the noise of the sensors, and every write is reported to every
// subscribed controller. The air quality index is written at every change.
static constexpr int32_t kTemperatureStep = 10; // 0.1 °C
static constexpr int32_t kHumidityStep = 50;    // 0.5 %
static constexpr int32_t kTvocStep = 10;        // ppb
#if CONFIG_SENSOR_ECO2_AS_CO2
static constexpr int32_t kEco2Step = 10; // ppm
#endif

static uint16_t s_temperature_ep;
static uint16_t s_humidity_ep;
static uint16_t s_air_ep;

// Sensor task only
static ReportedValue s_temperature;
static ReportedValue s_humidity;
static ReportedValue s_air_quality;
static ReportedValue s_tvoc;
#if CONFIG_SENSOR_ECO2_AS_CO2
static ReportedValue s_eco2;
#endif

// Numeric Measurement feature only (MeasuredValue, MinMeasuredValue, MaxMeasuredValue, MeasurementUnit), medium air.
static cluster_t *create_concentration(endpoint_t *endpoint, ConcentrationCreate create, uint16_t min, uint16_t max,
                                       MeasurementUnitEnum unit)
{
    cluster::concentration_measurement::config_t config;
    config.measurement_medium = chip::to_underlying(MeasurementMediumEnum::kAir);
    config.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
    config.features.numeric_measurement.measured_value = nullable<float>();
    config.features.numeric_measurement.min_measured_value = nullable<float>(min);
    config.features.numeric_measurement.max_measured_value = nullable<float>(max);
    config.features.numeric_measurement.measurement_unit = chip::to_underlying(unit);
    return create(endpoint, &config, CLUSTER_FLAG_SERVER);
}

// The optional TriggerEffect command of Identify (blink, okay, breathe...): the LED blinks for the effect.
static bool add_trigger_effect(endpoint_t *endpoint)
{
    cluster_t *identify = cluster::get(endpoint, Identify::Id);
    return identify != nullptr && cluster::identify::command::create_trigger_effect(identify) != nullptr;
}

esp_err_t sensor_endpoints_create(node_t *node)
{
    temperature_sensor::config_t temperature_config;
    temperature_config.temperature_measurement.min_measured_value = kTemperatureMin;
    temperature_config.temperature_measurement.max_measured_value = kTemperatureMax;
    endpoint_t *temperature = temperature_sensor::create(node, &temperature_config, ENDPOINT_FLAG_NONE, nullptr);
    VerifyOrReturnError(temperature != nullptr && add_trigger_effect(temperature), ESP_FAIL,
                        ESP_LOGE(TAG, "Failed to create the temperature endpoint"));
    s_temperature_ep = endpoint::get_id(temperature);

    humidity_sensor::config_t humidity_config;
    humidity_config.relative_humidity_measurement.min_measured_value = kHumidityMin;
    humidity_config.relative_humidity_measurement.max_measured_value = kHumidityMax;
    endpoint_t *humidity = humidity_sensor::create(node, &humidity_config, ENDPOINT_FLAG_NONE, nullptr);
    VerifyOrReturnError(humidity != nullptr && add_trigger_effect(humidity), ESP_FAIL,
                        ESP_LOGE(TAG, "Failed to create the humidity endpoint"));
    s_humidity_ep = endpoint::get_id(humidity);

    // AirQuality starts Unknown
    air_quality_sensor::config_t air_config;
    endpoint_t *air = air_quality_sensor::create(node, &air_config, ENDPOINT_FLAG_NONE, nullptr);
    VerifyOrReturnError(air != nullptr && add_trigger_effect(air), ESP_FAIL,
                        ESP_LOGE(TAG, "Failed to create the air quality endpoint"));
    s_air_ep = endpoint::get_id(air);
    // AQI-UBA 2, 3 and 5 are sent as Fair, Moderate and VeryPoor, values these features allow (Good, Poor and Unknown
    // are always allowed). The cluster reads its features when Matter starts.
    cluster_t *air_quality = cluster::get(air, AirQuality::Id);
    VerifyOrReturnError(air_quality != nullptr, ESP_FAIL);
    VerifyOrReturnError(cluster::air_quality::feature::fair::add(air_quality) == ESP_OK &&
                            cluster::air_quality::feature::moderate::add(air_quality) == ESP_OK &&
                            cluster::air_quality::feature::very_poor::add(air_quality) == ESP_OK,
                        ESP_FAIL, ESP_LOGE(TAG, "Failed to add the air quality levels"));

    const cluster_t *tvoc =
        create_concentration(air, cluster::total_volatile_organic_compounds_concentration_measurement::create, kTvocMin,
                             kTvocMax, MeasurementUnitEnum::kPpb);
    VerifyOrReturnError(tvoc != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the TVOC cluster"));
#if CONFIG_SENSOR_ECO2_AS_CO2
    const cluster_t *co2 = create_concentration(air, cluster::carbon_dioxide_concentration_measurement::create,
                                                kEco2Min, kEco2Max, MeasurementUnitEnum::kPpm);
    VerifyOrReturnError(co2 != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the CO2 (eCO2) cluster"));
    static const char kAirClusters[] = "air quality, TVOC, CO2 (eCO2)";
#else
    static const char kAirClusters[] = "air quality, TVOC";
#endif

    ESP_LOGI(TAG, "Endpoints: %u temperature, %u humidity, %u %s", s_temperature_ep, s_humidity_ep, s_air_ep,
             kAirClusters);
    return ESP_OK;
}

// esp-matter serves the Air Quality cluster with the AirQualityCluster of the Matter SDK, created when Matter starts
// (data_model_provider/clusters/air_quality/integration.cpp), and has no accessor for it, unlike Temperature
// Measurement: look it up in the registry of the data model. It is the only server of this cluster, so the cast is
// safe. Matter stack lock held.
static AirQualityCluster *air_quality_cluster()
{
    chip::app::ServerClusterInterface *cluster = data_model::provider::get_instance().registry().Get(
        chip::app::ConcreteClusterPath(s_air_ep, AirQuality::Id));
    return static_cast<AirQualityCluster *>(cluster);
}

// Matter stack lock held. Temperature Measurement, Relative Humidity Measurement and Air Quality are code-driven
// clusters of the Matter SDK: esp_matter::attribute::update() cannot change their attributes.
static void update_sdk_clusters(const sensor_values_t *v)
{
    const bool climate = v->climate_valid;
    const int16_t temperature = climate ? celsius_to_measured(v->temperature_c, kTemperatureMin, kTemperatureMax) : 0;
    if (s_temperature.due(!climate, temperature, kTemperatureStep)) {
        const CHIP_ERROR err = TemperatureMeasurement::SetMeasuredValue(
            s_temperature_ep, climate ? MakeNullable(temperature) : Nullable<int16_t>());
        if (err == CHIP_NO_ERROR) {
            s_temperature.set(!climate, temperature);
        } else {
            ESP_LOGW(TAG, "Temperature not set: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    const uint16_t humidity = climate ? humidity_to_measured(v->humidity_pct) : 0;
    if (s_humidity.due(!climate, humidity, kHumidityStep)) {
        const CHIP_ERROR err = RelativeHumidityMeasurement::SetMeasuredValue(
            s_humidity_ep, climate ? MakeNullable(humidity) : Nullable<uint16_t>());
        if (err == CHIP_NO_ERROR) {
            s_humidity.set(!climate, humidity);
        } else {
            ESP_LOGW(TAG, "Humidity not set: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    const uint8_t quality = v->air_valid ? ens160_aqi_to_air_quality(v->aqi) : 0; // 0: Unknown
    if (s_air_quality.due(false, quality, 1)) {
        AirQualityCluster *cluster = air_quality_cluster();
        if (cluster == nullptr) {
            ESP_LOGW(TAG, "Air quality not set: no Air Quality cluster on endpoint %u", s_air_ep);
        } else if (cluster->SetAirQuality(static_cast<AirQuality::AirQualityEnum>(quality)) ==
                   chip::Protocols::InteractionModel::Status::Success) {
            s_air_quality.set(false, quality);
        } else {
            ESP_LOGW(TAG, "Air quality %u not set", quality);
        }
    }
}

// The Concentration Measurement clusters live in the attribute storage of esp-matter: attribute::update() writes them
// and reports them. It takes the Matter stack lock itself.
static void update_concentration(ReportedValue &reported, uint32_t cluster_id, bool valid, uint16_t value,
                                 uint16_t min, uint16_t max, int32_t step)
{
    const uint16_t kept = value < min ? min : (value > max ? max : value);
    if (!reported.due(!valid, kept, step)) {
        return;
    }
    esp_matter_attr_val_t val = esp_matter_nullable_float(valid ? nullable<float>(kept) : nullable<float>());
    const esp_err_t err = attribute::update(s_air_ep, cluster_id, Tvoc::Attributes::MeasuredValue::Id, &val);
    if (err == ESP_OK) {
        reported.set(!valid, kept);
    } else {
        ESP_LOGW(TAG, "Cluster 0x%04" PRIX32 " MeasuredValue not set, err:%d", cluster_id, err);
    }
}

void sensor_endpoints_update(const sensor_values_t *values)
{
    {
        // Waits for the Matter task rather than posting to its event queue, which may be full.
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        update_sdk_clusters(values);
    }
    update_concentration(s_tvoc, Tvoc::Id, values->air_valid, values->tvoc_ppb, kTvocMin, kTvocMax, kTvocStep);
#if CONFIG_SENSOR_ECO2_AS_CO2
    update_concentration(s_eco2, CarbonDioxideConcentrationMeasurement::Id, values->air_valid, values->eco2_ppm,
                         kEco2Min, kEco2Max, kEco2Step);
#endif
}
