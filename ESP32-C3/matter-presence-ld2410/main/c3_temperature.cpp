/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <data_model_provider/clusters/temperature_measurement/integration.h>
#include <driver/temperature_sensor.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>

#include "c3_temperature.h"

static const char *TAG = "c3_temp";

// Measuring range of the sensor: 20 to 100 °C, error under 2 °C (ESP-IDF documentation). A running board is above
// 20 °C, and the range still covers a board that overheats.
static constexpr int kRangeMinC = 20;
static constexpr int kRangeMaxC = 100;
static constexpr chip::System::Clock::Seconds32 kReadPeriod{ 60 };
static constexpr int16_t kReportStep = 50; // 0.5 °C

static temperature_sensor_handle_t s_sensor;
static chip::EndpointId s_endpoint_id = chip::kInvalidEndpointId;
// Matter task
static bool s_reported;
static int16_t s_last;

esp_err_t c3_temperature_create()
{
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(kRangeMinC, kRangeMaxC);
    esp_err_t err = temperature_sensor_install(&config, &s_sensor);
    if (err == ESP_OK) {
        err = temperature_sensor_enable(s_sensor); // stays on: read once a minute
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internal temperature sensor not started, err:%d", err);
        return err;
    }

    esp_matter::endpoint::temperature_sensor::config_t endpoint_config;
    endpoint_config.temperature_measurement.min_measured_value = kC3TemperatureMin;
    endpoint_config.temperature_measurement.max_measured_value = kC3TemperatureMax;
    esp_matter::endpoint_t *endpoint = esp_matter::endpoint::temperature_sensor::create(
        esp_matter::node::get(), &endpoint_config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create the chip temperature endpoint");
        return ESP_FAIL;
    }
    s_endpoint_id = esp_matter::endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Chip temperature on endpoint %u", s_endpoint_id);
    return ESP_OK;
}

static void schedule_read();

// Matter task (Matter timer). The cluster reports MeasuredValue to the subscribed controllers when it changes.
static void read_temperature(chip::System::Layer *, void *)
{
    float celsius = 0;
    const esp_err_t err = temperature_sensor_get_celsius(s_sensor, &celsius);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reading the temperature failed, err:%d", err);
    } else {
        const int16_t value = c3_temperature_to_measured(celsius);
        if (c3_temperature_due(s_reported, s_last, value, kReportStep)) {
            const CHIP_ERROR set_err = chip::app::Clusters::TemperatureMeasurement::SetMeasuredValue(
                s_endpoint_id, chip::app::DataModel::MakeNullable(value));
            if (set_err == CHIP_NO_ERROR) {
                if (!s_reported) {
                    const int magnitude = value < 0 ? -value : value;
                    ESP_LOGI(TAG, "Chip temperature %s%d.%d C", value < 0 ? "-" : "", magnitude / 100,
                             magnitude % 100 / 10);
                }
                s_reported = true;
                s_last = value;
            } else {
                ESP_LOGW(TAG, "Chip temperature not set: %" CHIP_ERROR_FORMAT, set_err.Format());
            }
        }
    }
    schedule_read();
}

static void schedule_read()
{
    const CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(kReadPeriod, read_temperature, nullptr);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Chip temperature readings stopped, no timer: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void c3_temperature_start()
{
    if (s_endpoint_id == chip::kInvalidEndpointId) {
        return;
    }
    read_temperature(nullptr, nullptr);
}
