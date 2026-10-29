/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ens160.h"
#include "sensor_math.h"

static const char *TAG = "ens160";

static constexpr uint32_t kSpeedHz = 100 * 1000;
static constexpr int kTimeoutMs = 50;
// After an OPMODE or COMMAND write (the 10 ms boot time of the ScioSense driver)
static constexpr uint32_t kModeChangeMs = 10;

// Registers. Multi-byte registers are LSB first, and reads and writes go on to the next register.
static constexpr uint8_t kRegPartId = 0x00;
static constexpr uint8_t kRegOpMode = 0x10;
static constexpr uint8_t kRegCommand = 0x12;
static constexpr uint8_t kRegTempIn = 0x13;   // then RH_IN at 0x15
static constexpr uint8_t kRegStatus = 0x20;   // DEVICE_STATUS
static constexpr uint8_t kRegDataAqi = 0x21;  // then DATA_TVOC at 0x22 and DATA_ECO2 at 0x24
static constexpr uint8_t kRegGprRead4 = 0x4C; // GPR_READ bytes 4 to 6: firmware version after GET_APPVER

static constexpr uint8_t kModeIdle = 0x01;
static constexpr uint8_t kModeStandard = 0x02;
static constexpr uint8_t kModeReset = 0xF0;
// Commands, accepted in idle mode only
static constexpr uint8_t kCommandNop = 0x00;
static constexpr uint8_t kCommandGetAppVersion = 0x0E;
static constexpr uint8_t kCommandClearGpr = 0xCC;

static constexpr uint16_t kAddresses[] = {kEns160AddressHigh, kEns160AddressLow};

static i2c_master_dev_handle_t s_dev;
static uint16_t s_address;
static uint16_t s_part_id;
static uint16_t s_logged_part_id; // last wrong part ID logged: searched for at every reading, logged once

static const char *model()
{
    return s_part_id == kEns161PartId ? "ENS161" : "ENS160";
}

// vTaskDelay() ends on a tick boundary: wait one tick more than ms rounded up, so that the wait is never shorter.
static void delay_ms(uint32_t ms)
{
    const uint32_t tick_ms = configTICK_RATE_HZ >= 1000 ? 1 : 1000 / configTICK_RATE_HZ;
    vTaskDelay((ms + tick_ms - 1) / tick_ms + 1);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, kTimeoutMs);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), kTimeoutMs);
}

static esp_err_t set_mode(uint8_t mode)
{
    const esp_err_t err = write_reg(kRegOpMode, mode);
    delay_ms(kModeChangeMs);
    return err;
}

// Idle mode only. Logged once per initialization, for the support of a sensor that misbehaves.
static void log_firmware_version()
{
    uint8_t version[3] = {};
    esp_err_t err = write_reg(kRegCommand, kCommandNop);
    if (err == ESP_OK) {
        err = write_reg(kRegCommand, kCommandClearGpr);
        delay_ms(kModeChangeMs);
    }
    if (err == ESP_OK) {
        err = write_reg(kRegCommand, kCommandGetAppVersion);
        delay_ms(kModeChangeMs);
    }
    if (err == ESP_OK) {
        err = read_regs(kRegGprRead4, version, sizeof(version));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s at 0x%02X, firmware %u.%u.%u", model(), s_address, version[0], version[1], version[2]);
    } else {
        ESP_LOGW(TAG, "Firmware version not read, err:%d", err);
    }
}

static esp_err_t add_device(i2c_master_bus_handle_t bus, uint16_t address)
{
    if (s_dev != nullptr && s_address == address) {
        return ESP_OK;
    }
    if (s_dev != nullptr) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = nullptr;
        s_address = 0;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = kSpeedHz;
    const esp_err_t err = i2c_master_bus_add_device(bus, &config, &s_dev);
    if (err != ESP_OK) {
        s_dev = nullptr;
        return err;
    }
    s_address = address;
    return ESP_OK;
}

esp_err_t ens160_init(i2c_master_bus_handle_t bus, bool reset)
{
    uint16_t address = 0;
    for (const uint16_t candidate : kAddresses) {
        if (i2c_master_probe(bus, candidate, kTimeoutMs) == ESP_OK) {
            address = candidate;
            break;
        }
    }
    if (address == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = add_device(bus, address);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t id[2] = {};
    err = read_regs(kRegPartId, id, sizeof(id));
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t part_id = (uint16_t)(id[0] | (id[1] << 8));
    if (part_id != kEns160PartId && part_id != kEns161PartId) {
        if (part_id != s_logged_part_id) {
            s_logged_part_id = part_id;
            ESP_LOGE(TAG, "The device at 0x%02X has part ID 0x%04X: not an ENS160 (0x%04X) or ENS161 (0x%04X)",
                     address, part_id, kEns160PartId, kEns161PartId);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_part_id = part_id;

    if (reset) {
        err = set_mode(kModeReset);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        uint8_t mode = 0;
        uint8_t status = 0;
        err = read_regs(kRegOpMode, &mode, 1);
        if (err == ESP_OK) {
            err = read_regs(kRegStatus, &status, 1);
        }
        if (err != ESP_OK) {
            return err;
        }
        if (mode == kModeStandard && (status & kEns160StatusRunning) && !(status & kEns160StatusError)) {
            ESP_LOGI(TAG, "%s at 0x%02X already measuring (%s)", model(), address,
                     ens160_validity_name(ens160_validity(status)));
            return ESP_OK;
        }
    }

    // From deep sleep (power-on) or reset: idle mode for the commands, then the standard mode.
    err = set_mode(kModeIdle);
    if (err != ESP_OK) {
        return err;
    }
    log_firmware_version();
    err = set_mode(kModeStandard);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t mode = 0;
    err = read_regs(kRegOpMode, &mode, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (mode != kModeStandard) {
        ESP_LOGE(TAG, "The %s did not enter the standard mode (OPMODE 0x%02X)", model(), mode);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "%s at 0x%02X measuring (standard mode)", model(), address);
    return ESP_OK;
}

uint16_t ens160_address(void)
{
    return s_address;
}

esp_err_t ens160_set_compensation(float temperature_c, float humidity_pct)
{
    if (s_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t t = ens160_temp_in(temperature_c);
    const uint16_t rh = ens160_rh_in(humidity_pct);
    const uint8_t buf[5] = {kRegTempIn, (uint8_t)(t & 0xFF), (uint8_t)(t >> 8), (uint8_t)(rh & 0xFF),
                            (uint8_t)(rh >> 8)};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), kTimeoutMs);
}

esp_err_t ens160_read(ens160_data_t *data)
{
    if (s_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t status = 0;
    esp_err_t err = read_regs(kRegStatus, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    data->status = status;
    data->new_data = (status & kEns160StatusNewData) != 0;
    if (!data->new_data) {
        return ESP_OK;
    }
    // Reading the values clears NEWDAT.
    uint8_t values[5] = {};
    err = read_regs(kRegDataAqi, values, sizeof(values));
    if (err != ESP_OK) {
        data->new_data = false;
        return err;
    }
    data->aqi = values[0] & 0x07;
    data->tvoc_ppb = (uint16_t)(values[1] | (values[2] << 8));
    data->eco2_ppm = (uint16_t)(values[3] | (values[4] << 8));
    return ESP_OK;
}
