/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Sensirion SCD40 / SCD41 CO2 sensor on the I2C bus shared with the OLED. The
// sensor measures every 5 s on its own (periodic measurement mode); the task
// reads each new measurement and reports their average at the configured
// interval. Commands are 16-bit, and each 16-bit word the sensor sends is
// followed by its CRC-8.

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "scd4x.h"

static const char *TAG = "scd4x";

#define SCD4X_I2C_ADDRESS       0x62
// Slower than the OLED (400 kHz): more margin with weak pull-ups.
#define SCD4X_I2C_FREQ_HZ       100000
#define SCD4X_I2C_TIMEOUT_MS    100
#define SCD4X_TASK_STACK        3072
#define SCD4X_TASK_PRIO         5

#define CMD_START_PERIODIC      0x21B1
#define CMD_READ_MEASUREMENT    0xEC05      // 3 words: CO2 (ppm), temperature, humidity
#define CMD_STOP_PERIODIC       0x3F86
#define CMD_GET_DATA_READY      0xE4B8      // 1 word: a measurement is ready unless its 11 low bits are 0
#define CMD_GET_SERIAL_NUMBER   0x3682      // 3 words

// Time the sensor needs to execute a command before its response can be read.
#define CMD_EXEC_MS             1
// After stop_periodic_measurement, the sensor ignores commands for this long.
#define STOP_EXEC_MS            500
// From power-up until the sensor answers.
#define POWER_UP_MS             1000
// A new measurement comes every 5 s: look for it every second.
#define POLL_MS                 1000
// Measuring, but no new measurement for this long: the sensor was reset by a
// power glitch (idle again) or is stuck. It is started again.
#define RESTART_MS              15000
// Without a measurement for this long, the sensor is reported missing.
#define DATA_TIMEOUT_MS         30000
// While the sensor does not answer, try to start it this often.
#define START_RETRY_MS          10000

static scd4x_config_t s_config;
static i2c_master_dev_handle_t s_dev;

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

// Waits at least ms: vTaskDelay(n) ends at the n-th tick, and the first one
// may come right away.
static void wait_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms + portTICK_PERIOD_MS - 1) + 1);
}

// CRC-8 of the sensor: polynomial 0x31, initial value 0xFF (0xBEEF -> 0x92).
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_command(uint16_t cmd)
{
    const uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), SCD4X_I2C_TIMEOUT_MS);
}

// Sends cmd and reads its response: count words (at most 3), each checked
// with its CRC.
static esp_err_t read_words(uint16_t cmd, uint16_t *words, size_t count)
{
    uint8_t buf[3 * 3];
    if (count > 3) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = send_command(cmd);
    if (err != ESP_OK) {
        return err;
    }
    wait_ms(CMD_EXEC_MS);
    err = i2c_master_receive(s_dev, buf, 3 * count, SCD4X_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < count; i++) {
        const uint8_t *word = &buf[3 * i];
        if (crc8(word, 2) != word[2]) {
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = (uint16_t)((word[0] << 8) | word[1]);
    }
    return ESP_OK;
}

// The sensor keeps measuring while the ESP32 restarts, and accepts most
// commands only when idle: stop it first. Reading the serial number then
// checks that it answers before the measurements start.
static esp_err_t start_measurement()
{
    // A probe is silent when nothing answers, unlike a transfer: the I2C
    // driver logs each failed one.
    esp_err_t err = i2c_master_probe(s_config.bus, SCD4X_I2C_ADDRESS, SCD4X_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = send_command(CMD_STOP_PERIODIC);
    }
    if (err != ESP_OK) {
        return err;
    }
    wait_ms(STOP_EXEC_MS);
    uint16_t serial[3];
    err = read_words(CMD_GET_SERIAL_NUMBER, serial, 3);
    if (err == ESP_OK) {
        err = send_command(CMD_START_PERIODIC);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CO2 sensor %04X%04X%04X started", serial[0], serial[1], serial[2]);
    }
    return err;
}

// Reads the new measurement. ESP_ERR_NOT_FOUND: none since the last one.
static esp_err_t read_measurement(uint16_t *co2_ppm, float *temperature, float *humidity)
{
    uint16_t status;
    esp_err_t err = read_words(CMD_GET_DATA_READY, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    if ((status & 0x07FF) == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    uint16_t words[3];
    err = read_words(CMD_READ_MEASUREMENT, words, 3);
    if (err != ESP_OK) {
        return err;
    }
    *co2_ppm = words[0];
    *temperature = -45.0f + 175.0f * words[1] / 65535.0f;
    *humidity = 100.0f * words[2] / 65535.0f;
    return ESP_OK;
}

// Measurements since the last report.
typedef struct {
    float co2_sum;
    uint32_t count;
    float temperature;      // of the last one, only logged
    float humidity;         // of the last one, only logged
} co2_accumulator_t;

static void report_average(co2_accumulator_t *acc)
{
    // Whole ppm, as the sensor measures them.
    const scd4x_data_t data = { .valid = true, .co2_ppm = roundf(acc->co2_sum / acc->count) };
    // Temperature and humidity inside the sensor, which the board warms up:
    // only logged, as a check that the sensor works.
    ESP_LOGI(TAG, "CO2=%d ppm (sensor %d C, %d %%RH)", (int)data.co2_ppm, (int)lroundf(acc->temperature),
             (int)lroundf(acc->humidity));
    *acc = {};
    s_config.cb(&data, s_config.user_data);
}

static void report_invalid()
{
    ESP_LOGW(TAG, "No measurement from the CO2 sensor (I2C 0x%02X), check its wiring and power", SCD4X_I2C_ADDRESS);
    const scd4x_data_t data = { .valid = false, .co2_ppm = NAN };
    s_config.cb(&data, s_config.user_data);
}

static void scd4x_task(void *arg)
{
    bool measuring = false;
    bool start_failed = false;      // logged once, until the sensor starts
    int64_t next_start_ms = 0;
    bool reported_invalid = false;
    co2_accumulator_t acc = {};

    // Restart the device if this task ever stops (stale values are worse).
    esp_task_wdt_add(NULL);
    wait_ms(POWER_UP_MS);
    int64_t last_value_ms = now_ms();       // last measurement, or the start of the task
    int64_t started_ms = last_value_ms;     // last start of the measurements
    int64_t last_report_ms = last_value_ms;

    while (true) {
        esp_task_wdt_reset();
        int64_t now = now_ms();

        if (!measuring && now >= next_start_ms) {
            next_start_ms = now + START_RETRY_MS;
            const esp_err_t err = start_measurement();
            if (err == ESP_OK) {
                measuring = true;
                start_failed = false;
                started_ms = now_ms();      // the first measurement comes 5 s later
            } else if (!start_failed) {
                start_failed = true;
                ESP_LOGW(TAG, "CO2 sensor not answering (%s), retrying every %d s", esp_err_to_name(err),
                         START_RETRY_MS / 1000);
            }
        } else if (measuring) {
            uint16_t co2;
            float temperature, humidity;
            const esp_err_t err = read_measurement(&co2, &temperature, &humidity);
            // 0 ppm is an invalid sample: Sensirion's own driver skips it.
            if (err == ESP_OK && co2 > 0) {
                acc.co2_sum += co2;
                acc.count++;
                acc.temperature = temperature;
                acc.humidity = humidity;
                last_value_ms = now;
            } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_CRC) {
                // No answer (unplugged?): start it again, which probes it first.
                ESP_LOGW(TAG, "CO2 sensor read failed (%s)", esp_err_to_name(err));
                measuring = false;
            }
        }

        now = now_ms();
        if (measuring && now - (last_value_ms > started_ms ? last_value_ms : started_ms) >= RESTART_MS) {
            ESP_LOGW(TAG, "No new CO2 measurement for %d s, restarting the sensor", RESTART_MS / 1000);
            measuring = false;
            next_start_ms = now;
        }
        if (!reported_invalid && now - last_value_ms >= DATA_TIMEOUT_MS) {
            reported_invalid = true;
            acc = {};
            report_invalid();
        }

        if (acc.count > 0 && now - last_report_ms >= s_config.report_interval_ms) {
            last_report_ms = now;
            reported_invalid = false;
            report_average(&acc);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t scd4x_init(const scd4x_config_t *config)
{
    if (config == NULL || config->cb == NULL || config->report_interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->bus == NULL) {
        return ESP_ERR_INVALID_STATE;   // the I2C bus could not be created
    }
    s_config = *config;

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = SCD4X_I2C_ADDRESS;
    dev_config.scl_speed_hz = SCD4X_I2C_FREQ_HZ;
    esp_err_t err = i2c_master_bus_add_device(config->bus, &dev_config, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(scd4x_task, "scd4x", SCD4X_TASK_STACK, NULL, SCD4X_TASK_PRIO, NULL) != pdPASS) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CO2 sensor SCD4x at I2C 0x%02X, reported every %u s", SCD4X_I2C_ADDRESS,
             (unsigned)(config->report_interval_ms / 1000));
    return ESP_OK;
}
