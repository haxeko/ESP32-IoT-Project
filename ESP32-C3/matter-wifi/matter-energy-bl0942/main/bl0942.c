/*
 * BL0942 single-phase energy meter, UART interface.
 *
 * Protocol (BL0942 datasheet):
 *  - Read: host sends {0x58 | addr, REG}; REG = 0xAA reads the full parameter packet.
 *  - Reply, 23 bytes: 0x55, I_RMS[3], V_RMS[3], I_FAST_RMS[3], WATT[3], CF_CNT[3], FREQ[2],
 *    STATUS[3], CHECKSUM. Little-endian.
 *  - CHECKSUM = ~((0x58 | addr) + sum of the first 22 bytes) & 0xFF
 */
#include "bl0942.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "bl0942";

#define BL0942_READ_CMD         0x58
#define BL0942_FULL_PACKET      0xAA
#define BL0942_PACKET_HEADER    0x55
#define BL0942_PACKET_LEN       23
#define BL0942_UART_BUF_SIZE    256
#define BL0942_READ_TIMEOUT_MS  200

/* Readings below these are noise with no load connected */
#define BL0942_CURRENT_NOISE_A  0.015f
#define BL0942_POWER_NOISE_W    0.5f

static const uart_port_t s_uart = (uart_port_t)CONFIG_BL0942_UART_PORT;
static const uint8_t s_cmd = BL0942_READ_CMD | CONFIG_BL0942_ADDRESS;

static float s_voltage_ref;
static float s_current_ref;
static float s_power_ref;
static double s_mwh_per_pulse;
static uint32_t s_max_pulses;

static uint64_t s_energy_base_mwh;
static double s_energy_counted_mwh;
static int32_t s_last_cf = -1;
static int s_fail_count;
static int s_baud = CONFIG_BL0942_UART_BAUD;
static bool s_baud_confirmed;
static bl0942_reading_t s_last;

typedef struct {
    uint32_t i_rms;
    uint32_t v_rms;
    int32_t watt;
    uint32_t cf_cnt;
    uint16_t freq;
} bl0942_raw_t;

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

static float cfg_float(const char *s, float fallback)
{
    float v = strtof(s, NULL);
    return v > 0 ? v : fallback;
}

static esp_err_t bl0942_read_raw(bl0942_raw_t *out)
{
    const uint8_t cmd[2] = { s_cmd, BL0942_FULL_PACKET };

    /* Drop stale bytes so the reply starts on a frame boundary */
    uart_flush_input(s_uart);
    if (uart_write_bytes(s_uart, cmd, sizeof(cmd)) != sizeof(cmd)) {
        return ESP_FAIL;
    }

    uint8_t buf[BL0942_PACKET_LEN];
    int len = uart_read_bytes(s_uart, buf, sizeof(buf), pdMS_TO_TICKS(BL0942_READ_TIMEOUT_MS));
    if (len != BL0942_PACKET_LEN) {
        ESP_LOGD(TAG, "Timeout: received %d/%d bytes", len < 0 ? 0 : len, BL0942_PACKET_LEN);
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] != BL0942_PACKET_HEADER) {
        ESP_LOGW(TAG, "Bad header 0x%02x", buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t sum = s_cmd;
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
    out->watt = le24_signed(&buf[10]);
    out->cf_cnt = le24(&buf[13]);
    out->freq = (uint16_t)(buf[16] | (buf[17] << 8));
    return ESP_OK;
}

esp_err_t bl0942_init(uint64_t energy_base_mwh)
{
    s_energy_base_mwh = energy_base_mwh;

    s_voltage_ref = cfg_float(CONFIG_BL0942_VOLTAGE_REF, 15873.35944f) * 1000.0f / CONFIG_BL0942_VOLTAGE_CAL_PERMILLE;
    s_current_ref = cfg_float(CONFIG_BL0942_CURRENT_REF, 251213.46469f) * 1000.0f / CONFIG_BL0942_CURRENT_CAL_PERMILLE;
    s_power_ref = cfg_float(CONFIG_BL0942_POWER_REF, 596.0f) * 1000.0f / CONFIG_BL0942_POWER_CAL_PERMILLE;
    /* One CF pulse is 1638.4 * 256 / POWER_REF W.s; the power calibration also applies to energy */
    float energy_ref = cfg_float(CONFIG_BL0942_ENERGY_REF, 0);
    energy_ref = energy_ref > 0 ? energy_ref * 1000.0f / CONFIG_BL0942_POWER_CAL_PERMILLE
                                : s_power_ref * 3600000.0f / 419430.4f;
    s_mwh_per_pulse = 1e6 / energy_ref;
    /* Upper bound of CF pulses between two reads (~20 kW); a bigger jump means the BL0942 was reset */
    s_max_pulses = (uint32_t)(energy_ref * 20.0f * CONFIG_BL0942_UPDATE_INTERVAL_MS / 3600000.0f) + 10;

    const uart_config_t uart_cfg = {
        .baud_rate = s_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(s_uart, BL0942_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(s_uart, &uart_cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(s_uart, CONFIG_BL0942_UART_TX_GPIO, CONFIG_BL0942_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        uart_driver_delete(s_uart);
        return err;
    }

    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d %d baud, Vref=%.2f Iref=%.2f Pref=%.3f Eref=%.2f pulse/kWh", s_uart,
             CONFIG_BL0942_UART_RX_GPIO, CONFIG_BL0942_UART_TX_GPIO, s_baud, s_voltage_ref,
             s_current_ref, s_power_ref, energy_ref);
    return ESP_OK;
}

void bl0942_update(bl0942_reading_t *out)
{
    bl0942_raw_t raw;
    if (bl0942_read_raw(&raw) != ESP_OK) {
        /* Keep the last values for a few misses, then report the meter as unavailable */
        ++s_fail_count;
        if (s_fail_count == 3) {
            ESP_LOGE(TAG, "BL0942 not responding: check TX->GPIO%d, RX->GPIO%d, baud %d", CONFIG_BL0942_UART_RX_GPIO,
                     CONFIG_BL0942_UART_TX_GPIO, s_baud);
        }
        /* The BL0942 baud rate is set by its SCLK_BPS pin (low: 4800, high: 9600).
         * Until a valid packet has been received, alternate between the two. */
        if (!s_baud_confirmed && s_fail_count % 3 == 0) {
            s_baud = (s_baud == 4800) ? 9600 : 4800;
            uart_set_baudrate(s_uart, s_baud);
            ESP_LOGW(TAG, "Trying %d baud", s_baud);
        }
        *out = s_last;
        if (s_fail_count >= 3) {
            out->valid = false;
        }
        out->energy_mwh = s_energy_base_mwh + (uint64_t)s_energy_counted_mwh;
        return;
    }
    s_fail_count = 0;
    if (!s_baud_confirmed) {
        s_baud_confirmed = true;
        ESP_LOGI(TAG, "BL0942 answering at %d baud", s_baud);
    }

    /* Energy from the 24-bit CF pulse counter */
    const uint32_t cf = raw.cf_cnt & 0xFFFFFF;
    if (s_last_cf >= 0) {
        const uint32_t delta = (cf - (uint32_t)s_last_cf) & 0xFFFFFF;
        if (delta <= s_max_pulses) {
            s_energy_counted_mwh += delta * s_mwh_per_pulse;
        } else {
            ESP_LOGW(TAG, "CF_CNT jump %ld -> %lu ignored", (long)s_last_cf, (unsigned long)cf);
        }
    }
    s_last_cf = (int32_t)cf;

    float current = raw.i_rms / s_current_ref;
    float power = raw.watt / s_power_ref;
    if (current < BL0942_CURRENT_NOISE_A) {
        current = 0;
    }
    if (fabsf(power) < BL0942_POWER_NOISE_W) {
        power = 0;
    }

    out->valid = true;
    out->voltage_v = raw.v_rms / s_voltage_ref;
    out->current_a = current;
    out->power_w = power;
    out->frequency_hz = raw.freq ? 1000000.0f / raw.freq : 0;
    out->energy_mwh = s_energy_base_mwh + (uint64_t)s_energy_counted_mwh;
    s_last = *out;
}
