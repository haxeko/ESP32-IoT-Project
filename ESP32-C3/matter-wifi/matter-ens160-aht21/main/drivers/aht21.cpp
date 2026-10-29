/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "aht21.h"
#include "sensor_math.h"

static const char *TAG = "aht21";

static constexpr uint32_t kSpeedHz = 100 * 1000;
static constexpr int kTimeoutMs = 50;
static constexpr uint32_t kMeasureMs = 80;  // datasheet: wait 80 ms, then read until the busy bit is clear
static constexpr int kBusyReads = 6;        // 80 ms + 5 x 10 ms at most
static constexpr uint8_t kCmdSoftReset = 0xBA;
static constexpr uint8_t kCmdMeasure[3] = {0xAC, 0x33, 0x00};
static constexpr uint8_t kCmdCalibrate[3] = {0xBE, 0x08, 0x00}; // first AHT20 series; the AHT21 uses the registers
static constexpr uint8_t kInitRegisters[3] = {0x1B, 0x1C, 0x1E};

static i2c_master_dev_handle_t s_dev;

// vTaskDelay() ends on a tick boundary: wait one tick more than ms rounded up, so that the wait is never shorter.
static void delay_ms(uint32_t ms)
{
    const uint32_t tick_ms = configTICK_RATE_HZ >= 1000 ? 1 : 1000 / configTICK_RATE_HZ;
    vTaskDelay((ms + tick_ms - 1) / tick_ms + 1);
}

static esp_err_t read_status(uint8_t *status)
{
    return i2c_master_receive(s_dev, status, 1, kTimeoutMs);
}

// Initialization of one of the registers 0x1B, 0x1C and 0x1E, as in the sample code of Aosong: select it, read it
// back, write it again with 0xB0 | register.
static esp_err_t init_register(uint8_t reg)
{
    const uint8_t select[3] = {reg, 0x00, 0x00};
    esp_err_t err = i2c_master_transmit(s_dev, select, sizeof(select), kTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }
    delay_ms(5);
    uint8_t value[3] = {};
    err = i2c_master_receive(s_dev, value, sizeof(value), kTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }
    delay_ms(10);
    const uint8_t write[3] = {(uint8_t)(0xB0 | reg), value[1], value[2]};
    return i2c_master_transmit(s_dev, write, sizeof(write), kTimeoutMs);
}

esp_err_t aht21_init(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, kAht21Address, kTimeoutMs) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err;
    if (s_dev == nullptr) {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = kAht21Address;
        config.scl_speed_hz = kSpeedHz;
        err = i2c_master_bus_add_device(bus, &config, &s_dev);
        if (err != ESP_OK) {
            s_dev = nullptr;
            return err;
        }
    }

    // Also brings a sensor left in a bad state (power glitch, a reboot in the middle of a read) back to the state
    // after power-on.
    err = i2c_master_transmit(s_dev, &kCmdSoftReset, 1, kTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }
    delay_ms(20);

    uint8_t status = 0;
    err = read_status(&status);
    if (err != ESP_OK) {
        return err;
    }
    if ((status & kAht2xStatusReady) != kAht2xStatusReady) {
        ESP_LOGI(TAG, "Status 0x%02X: initializing the calibration registers", status);
        for (const uint8_t reg : kInitRegisters) {
            err = init_register(reg);
            if (err != ESP_OK) {
                return err;
            }
        }
        delay_ms(10);
        err = read_status(&status);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!(status & kAht2xStatusCalibrated)) {
        err = i2c_master_transmit(s_dev, kCmdCalibrate, sizeof(kCmdCalibrate), kTimeoutMs);
        if (err != ESP_OK) {
            return err;
        }
        delay_ms(10);
        err = read_status(&status);
        if (err != ESP_OK) {
            return err;
        }
        if (!(status & kAht2xStatusCalibrated)) {
            ESP_LOGW(TAG, "Status 0x%02X: the sensor does not report itself calibrated", status);
        }
    }
    ESP_LOGD(TAG, "AHT21 at 0x%02X initialized, status 0x%02X", kAht21Address, status);
    return ESP_OK;
}

esp_err_t aht21_read(float *temperature_c, float *humidity_pct)
{
    if (s_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit(s_dev, kCmdMeasure, sizeof(kCmdMeasure), kTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }
    delay_ms(kMeasureMs);
    for (int read = 0; read < kBusyReads; read++) {
        if (read > 0) {
            delay_ms(10);
        }
        uint8_t frame[7];
        err = i2c_master_receive(s_dev, frame, sizeof(frame), kTimeoutMs);
        if (err != ESP_OK) {
            return err;
        }
        switch (aht2x_parse(frame, temperature_c, humidity_pct)) {
        case AHT2X_FRAME_OK:
            return ESP_OK;
        case AHT2X_FRAME_BAD_CRC:
            return ESP_ERR_INVALID_CRC;
        case AHT2X_FRAME_NOT_CALIBRATED:
            return ESP_ERR_INVALID_STATE;
        case AHT2X_FRAME_BUSY:
            break;
        }
    }
    return ESP_ERR_TIMEOUT;
}
