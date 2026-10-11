/*
 * BL0942 UART driver.
 *
 * Giao thức (datasheet BL0942 V1.06):
 *  - Đọc:  host gửi {0x58 | addr, REG}; REG = 0xAA đọc gói toàn bộ thông số.
 *  - Gói trả về 23 byte: 0x55, I_RMS[3], V_RMS[3], I_FAST_RMS[3], WATT[3],
 *    CF_CNT[3], FREQ[2], STATUS[3], CHECKSUM. Dữ liệu little-endian.
 *  - CHECKSUM = ~((0x58 | addr) + tổng 22 byte đầu) & 0xFF
 */
#include "bl0942.h"

#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "bl0942";

#define BL0942_READ_CMD         0x58
#define BL0942_FULL_PACKET      0xAA
#define BL0942_PACKET_HEADER    0x55
#define BL0942_PACKET_LEN       23
#define BL0942_UART_BUF_SIZE    256

static bl0942_config_t s_cfg;
static bool s_initialized;

static inline uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline int32_t le24_signed(const uint8_t *p)
{
    uint32_t v = le24(p);
    if (v & 0x800000) {
        v |= 0xFF000000;
    }
    return (int32_t)v;
}

esp_err_t bl0942_init(const bl0942_config_t *config)
{
    if (!config || config->address > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;

    const uart_config_t uart_cfg = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(config->uart_port, BL0942_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(config->uart_port, &uart_cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(config->uart_port, config->tx_gpio, config->rx_gpio, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        uart_driver_delete(config->uart_port);
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART%d init: RX=GPIO%d TX=GPIO%d %d baud addr=%d", config->uart_port, config->rx_gpio,
             config->tx_gpio, config->baud_rate, config->address);
    return ESP_OK;
}

esp_err_t bl0942_read_raw(bl0942_raw_t *out, uint32_t timeout_ms)
{
    if (!s_initialized || !out) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t cmd_byte = BL0942_READ_CMD | s_cfg.address;
    const uint8_t cmd[2] = { cmd_byte, BL0942_FULL_PACKET };

    /* Bỏ dữ liệu cũ còn trong bộ đệm để đồng bộ khung */
    uart_flush_input(s_cfg.uart_port);
    if (uart_write_bytes(s_cfg.uart_port, cmd, sizeof(cmd)) != sizeof(cmd)) {
        return ESP_FAIL;
    }

    uint8_t buf[BL0942_PACKET_LEN];
    int len = uart_read_bytes(s_cfg.uart_port, buf, sizeof(buf), pdMS_TO_TICKS(timeout_ms));
    if (len != BL0942_PACKET_LEN) {
        ESP_LOGW(TAG, "Timeout: received %d/%d bytes", len < 0 ? 0 : len, BL0942_PACKET_LEN);
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] != BL0942_PACKET_HEADER) {
        ESP_LOGW(TAG, "Bad header 0x%02x", buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t sum = cmd_byte;
    for (int i = 0; i < BL0942_PACKET_LEN - 1; i++) {
        sum += buf[i];
    }
    sum = ~sum;
    if (sum != buf[BL0942_PACKET_LEN - 1]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc 0x%02x, got 0x%02x", sum, buf[BL0942_PACKET_LEN - 1]);
        return ESP_ERR_INVALID_CRC;
    }

    out->i_rms = le24(&buf[1]);
    out->v_rms = le24(&buf[4]);
    out->i_fast_rms = le24(&buf[7]);
    out->watt = le24_signed(&buf[10]);
    out->cf_cnt = le24(&buf[13]);
    out->freq = (uint16_t)(buf[16] | (buf[17] << 8));
    out->status = le24(&buf[18]);
    return ESP_OK;
}
