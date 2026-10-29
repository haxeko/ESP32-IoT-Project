/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <stdlib.h>

#include <drivers/aht21.h>
#include <drivers/ens160.h>
#include <drivers/sensor_math.h>

#include "sensors.h"

static const char *TAG = "sensors";

static constexpr uint32_t kTaskStack = 4096;
// Above the Matter task (CONFIG_CHIP_TASK_PRIORITY = 1), below the Wi-Fi and lwIP tasks; it sleeps between readings.
static constexpr UBaseType_t kTaskPriority = 2;
// Readings in a row without a value before a sensor counts as lost: until then its last values are kept, then it
// has none (null in Matter) and is initialized again.
static constexpr uint8_t kMaxFailures = 3;
// One log line of the values every minute, and at the first reading
static constexpr uint32_t kLogPeriodSec = 60;
static constexpr float kTemperatureOffsetC = CONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS / 10.0f;
// Kconfig leaves a bool option undefined when it is off.
#if CONFIG_ENS160_REPORT_WHILE_WARMING_UP
static constexpr bool kReportWhileWarmingUp = true;
#else
static constexpr bool kReportWhileWarmingUp = false;
#endif

// A sensor on the bus
struct SensorLink {
    bool ready = false;      // initialized: read at every reading
    bool present = false;    // answered its last initialization
    bool logged = false;     // its presence was logged (then only its changes are)
    uint8_t failures = 0;    // readings in a row without a value
    bool has_values = false; // its last values are reported: kept for kMaxFailures readings without a new one
};

static i2c_master_bus_handle_t s_bus;
static sensor_values_cb_t s_cb;

static SensorLink s_aht;
static float s_temperature_c; // last AHT21 values, before the offset
static float s_humidity_pct;

static SensorLink s_ens;
static bool s_ens_reset;    // reset the ENS160 at its next initialization: it stopped measuring
static ens160_data_t s_air; // last ENS160 status and values
static int s_logged_validity = -1;

// Counts a reading without a value. From kMaxFailures in a row on, the sensor has no values and is initialized again
// at the next reading. Returns true when this reading made it lose its values.
static bool count_failure(SensorLink &link)
{
    if (link.failures < UINT8_MAX) {
        link.failures++;
    }
    if (link.failures < kMaxFailures) {
        return false;
    }
    link.ready = false;
    // Frees SDA if a sensor holds it low after a transfer cut in the middle (9 clock pulses and a stop)
    i2c_master_bus_reset(s_bus);
    const bool lost = link.has_values;
    link.has_values = false;
    return lost;
}

// After an initialization: logs the sensor found or missing, the first time and when it changes.
static bool log_presence(SensorLink &link, bool present)
{
    const bool changed = !link.logged || present != link.present;
    link.logged = true;
    link.present = present;
    return changed;
}

static void read_climate(sensor_values_t *values)
{
    esp_err_t err = ESP_OK;
    if (!s_aht.ready) {
        err = aht21_init(s_bus);
        s_aht.ready = err == ESP_OK;
        if (log_presence(s_aht, s_aht.ready)) {
            if (s_aht.ready) {
                ESP_LOGI(TAG, "AHT21 found at 0x%02X", kAht21Address);
            } else {
                ESP_LOGW(TAG,
                         "No AHT21 at 0x%02X (err:%d): no temperature and humidity. The ENS160 measures no "
                         "temperature, the AHT21 of the module does: check SDA GPIO%d, SCL GPIO%d, 3V3 and GND",
                         kAht21Address, err, CONFIG_SENSOR_I2C_SDA_GPIO, CONFIG_SENSOR_I2C_SCL_GPIO);
            }
        }
    }

    float t = NAN;
    float rh = NAN;
    if (err == ESP_OK) {
        err = aht21_read(&t, &rh);
        if (err == ESP_OK) {
            s_aht.failures = 0;
            s_aht.has_values = true;
            s_temperature_c = t;
            s_humidity_pct = rh;
        } else {
            if (s_aht.has_values) {
                ESP_LOGW(TAG, "AHT21 reading failed, err:%d", err);
            } else {
                ESP_LOGD(TAG, "AHT21 reading failed, err:%d", err);
            }
            if (err == ESP_ERR_INVALID_STATE) {
                s_aht.ready = false; // lost its initialization (power glitch): initialized again at the next reading
            }
        }
    }
    if (err != ESP_OK && count_failure(s_aht)) {
        ESP_LOGW(TAG, "AHT21 lost after %u readings without values: temperature and humidity unknown", kMaxFailures);
    }
    if (!s_aht.has_values) {
        return;
    }
    values->climate_valid = true;
    values->temperature_c = s_temperature_c + kTemperatureOffsetC;
    // The offset is only a correction of the temperature: the air holds the same water vapour, at another temperature
    values->humidity_pct = kTemperatureOffsetC == 0.0f
                               ? s_humidity_pct
                               : humidity_at_temperature(s_humidity_pct, s_temperature_c, values->temperature_c);
}

static void log_validity(ens160_validity_t validity)
{
    if ((int)validity == s_logged_validity) {
        return;
    }
    s_logged_validity = (int)validity;
    const bool sent = validity == ENS160_VALIDITY_NORMAL || kReportWhileWarmingUp;
    if (validity == ENS160_VALIDITY_INVALID) {
        ESP_LOGW(TAG, "ENS160: %s, air quality unknown", ens160_validity_name(validity));
    } else {
        ESP_LOGI(TAG, "ENS160: %s%s", ens160_validity_name(validity),
                 sent ? "" : ", the air quality is sent once the values are valid");
    }
}

static void read_air(sensor_values_t *values)
{
    esp_err_t err = ESP_OK;
    if (!s_ens.ready) {
        err = ens160_init(s_bus, s_ens_reset);
        s_ens.ready = err == ESP_OK;
        // Found: the driver logs its address and mode
        if (log_presence(s_ens, s_ens.ready) && !s_ens.ready) {
            ESP_LOGW(TAG, "No ENS160 at 0x%02X or 0x%02X (err:%d): air quality, TVOC and eCO2 unknown",
                     kEns160AddressHigh, kEns160AddressLow, err);
        }
        if (s_ens.ready) {
            // It may have no values in its first second: this reading and the next ones get the whole budget.
            // Its values were dropped when it was lost, so none are kept from before.
            s_ens.failures = 0;
            s_ens_reset = false;
            s_logged_validity = -1;
        }
    }

    if (err == ESP_OK && values->climate_valid) {
        err = ens160_set_compensation(values->temperature_c, values->humidity_pct);
    }
    ens160_data_t data = s_air; // the values stay when the sensor has no new ones
    if (err == ESP_OK) {
        err = ens160_read(&data);
    }
    if (err == ESP_OK) {
        s_air.status = data.status;
    }
    const bool fresh = err == ESP_OK && data.new_data && !(data.status & kEns160StatusError);
    if (fresh) {
        s_ens.failures = 0;
        s_ens.has_values = true;
        s_air = data;
    } else {
        if (!s_ens.ready) {
            // Not found: logged above
        } else if (err != ESP_OK) {
            if (s_ens.has_values) {
                ESP_LOGW(TAG, "ENS160 reading failed, err:%d", err);
            } else {
                ESP_LOGD(TAG, "ENS160 reading failed, err:%d", err);
            }
        } else if (data.status & kEns160StatusError) {
            ESP_LOGW(TAG, "ENS160 reports an error (status 0x%02X)", data.status);
        } else {
            ESP_LOGD(TAG, "ENS160: no new values (status 0x%02X)", data.status);
        }
        if (count_failure(s_ens)) {
            ESP_LOGW(TAG, "ENS160 lost after %u readings without values: air quality unknown", kMaxFailures);
        }
        if (!s_ens.ready) {
            // Stopped answering or measuring: reset it at its next initialization (it warms up again).
            s_ens_reset = true;
        }
    }
    values->air_found = s_ens.ready || s_ens.has_values;
    if (!s_ens.has_values) {
        return;
    }
    const ens160_validity_t validity = ens160_validity(s_air.status);
    log_validity(validity);
    if (validity == ENS160_VALIDITY_INVALID || (validity != ENS160_VALIDITY_NORMAL && !kReportWhileWarmingUp)) {
        return;
    }
    values->air_valid = true;
    values->aqi = s_air.aqi;
    values->tvoc_ppb = s_air.tvoc_ppb;
    values->eco2_ppm = s_air.eco2_ppm;
}

esp_err_t sensors_init_bus(void)
{
    i2c_master_bus_config_t config = {};
    config.i2c_port = -1;
    config.sda_io_num = (gpio_num_t)CONFIG_SENSOR_I2C_SDA_GPIO;
    config.scl_io_num = (gpio_num_t)CONFIG_SENSOR_I2C_SCL_GPIO;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    // The module has its own pull-up resistors; the internal ones (about 45 kOhm) only help a bare sensor.
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, &s_bus);
}

void sensors_read_once(sensor_values_t *values)
{
    *values = {};
    read_climate(values);
    read_air(values);
}

void sensors_log(const sensor_values_t *v)
{
    char climate[40] = "no AHT21";
    if (v->climate_valid) {
        const int t = celsius_to_measured(v->temperature_c, -32767, 32767);
        const int rh = humidity_to_measured(v->humidity_pct);
        snprintf(climate, sizeof(climate), "%s%d.%02d C, %d.%02d %%RH", t < 0 ? "-" : "", abs(t) / 100, abs(t) % 100,
                 rh / 100, rh % 100);
    }
    char air[72] = "no ENS160";
    if (s_ens.has_values) {
        snprintf(air, sizeof(air), "AQI %u, TVOC %u ppb, eCO2 %u ppm%s", s_air.aqi, s_air.tvoc_ppb, s_air.eco2_ppm,
                 v->air_valid ? "" : " (not valid yet, not sent)");
    } else if (s_ens.ready) {
        snprintf(air, sizeof(air), "ENS160 starting");
    }
    ESP_LOGI(TAG, "%s | %s", climate, air);
}

static void sensor_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(100)); // the AHT21 needs 100 ms after power-on before its first command
    const uint32_t log_every = kLogPeriodSec > CONFIG_SENSOR_PERIOD_SEC ? kLogPeriodSec / CONFIG_SENSOR_PERIOD_SEC : 1;
    uint32_t readings = 0;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        sensor_values_t values;
        sensors_read_once(&values);
        if (readings++ % log_every == 0) {
            sensors_log(&values);
        }
        s_cb(&values);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_SENSOR_PERIOD_SEC * 1000));
    }
}

esp_err_t sensors_start(sensor_values_cb_t cb)
{
    s_cb = cb;
    esp_err_t err = sensors_init_bus();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus on SDA GPIO%d, SCL GPIO%d not started, err:%d", CONFIG_SENSOR_I2C_SDA_GPIO,
                 CONFIG_SENSOR_I2C_SCL_GPIO, err);
        return err;
    }
    const int offset = CONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS;
    ESP_LOGI(TAG, "ENS160 + AHT21 on I2C SDA GPIO%d, SCL GPIO%d, reading every %d s, temperature offset %s%d.%d C",
             CONFIG_SENSOR_I2C_SDA_GPIO, CONFIG_SENSOR_I2C_SCL_GPIO, CONFIG_SENSOR_PERIOD_SEC, offset < 0 ? "-" : "",
             abs(offset) / 10, abs(offset) % 10);
    if (xTaskCreate(sensor_task, "sensors", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
