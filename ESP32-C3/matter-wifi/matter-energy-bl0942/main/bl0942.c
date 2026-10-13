/*
 * BL0942 single-phase energy meter, UART interface.
 *
 * Protocol (BL0942 datasheet, same as the ESPHome bl0942 component):
 *  - Read: host sends {0x58 | addr, REG}; REG = 0xAA reads the full parameter packet.
 *  - Reply, 23 bytes: 0x55, I_RMS[3], V_RMS[3], I_FAST_RMS[3], WATT[3], CF_CNT[3], FREQ[2],
 *    STATUS[3], CHECKSUM. Little-endian.
 *  - CHECKSUM = ~((0x58 | addr) + sum of the first 22 bytes) & 0xFF
 *  - Write: {0xA8 | addr, REG, D0, D1, D2, ~(sum of the first 5 bytes)}
 */
#include "bl0942.h"

#include <math.h>
#include <stdlib.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "bl0942";

#define BL0942_READ_CMD         0x58
#define BL0942_WRITE_CMD        0xA8
#define BL0942_FULL_PACKET      0xAA
#define BL0942_PACKET_HEADER    0x55
#define BL0942_PACKET_LEN       23
#define BL0942_UART_BUF_SIZE    256
#define BL0942_READ_TIMEOUT_MS  200

#define BL0942_REG_MODE         0x19
#define BL0942_REG_USR_WRPROT   0x1D
#define BL0942_WRPROT_UNLOCK    0x55
/* MODE as set by ESPHome: RESV | CF_EN | CF_CNT_ADD_SEL (absolute energy) | RMS_UPDATE_SEL (800 ms), 50 Hz */
#define BL0942_MODE_VALUE       (0x03 | 0x04 | 0x80 | 0x08)

/* Missed reads before the meter is reported as unavailable, and before the setup is sent again */
#define BL0942_MAX_MISSES       3
#define BL0942_SETUP_RETRY      10

/* Readings below these are noise with no load connected */
#define BL0942_CURRENT_NOISE_A  0.015f
#define BL0942_POWER_NOISE_W    0.5f

/* Highest plausible power, used to reject CF_CNT jumps (BL0942 reset) */
#define BL0942_MAX_POWER_KW     20.0

static const uart_port_t s_uart = (uart_port_t)CONFIG_BL0942_UART_PORT;
static const uint8_t s_address = CONFIG_BL0942_ADDRESS;

static float s_voltage_ref;
static float s_current_ref;
static float s_power_ref;
static double s_mwh_per_pulse;
static double s_pulses_per_kwh;

static uint64_t s_energy_base_mwh;
static double s_energy_counted_mwh;
static int32_t s_last_cf = -1;
static int64_t s_last_cf_us;
static int s_fail_count;
static bl0942_reading_t s_last;

typedef struct {
    uint32_t i_rms;
    uint32_t v_rms;
    int32_t watt;
    uint32_t cf_cnt;
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

static void write_reg(uint8_t reg, uint32_t val)
{
    uint8_t pkt[6];
    pkt[0] = BL0942_WRITE_CMD | s_address;
    pkt[1] = reg;
    pkt[2] = val & 0xFF;
    pkt[3] = (val >> 8) & 0xFF;
    pkt[4] = (val >> 16) & 0xFF;
    pkt[5] = (uint8_t)((pkt[0] + pkt[1] + pkt[2] + pkt[3] + pkt[4]) ^ 0xFF);
    uart_write_bytes(s_uart, pkt, sizeof(pkt));
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(2));
}

/* Configure the BL0942 the same way ESPHome does: unlock, MODE, lock */
static void bl0942_setup(void)
{
    write_reg(BL0942_REG_USR_WRPROT, BL0942_WRPROT_UNLOCK);
    write_reg(BL0942_REG_MODE, BL0942_MODE_VALUE);
    write_reg(BL0942_REG_USR_WRPROT, 0);
    uart_flush_input(s_uart);
}

static esp_err_t bl0942_read_raw(bl0942_raw_t *out)
{
    const uint8_t cmd_byte = BL0942_READ_CMD | s_address;
    const uint8_t cmd[2] = { cmd_byte, BL0942_FULL_PACKET };

    /* Drop stale bytes so the reply starts on a frame boundary */
    uart_flush_input(s_uart);
    if (uart_write_bytes(s_uart, cmd, sizeof(cmd)) != sizeof(cmd)) {
        return ESP_FAIL;
    }

    uint8_t buf[BL0942_PACKET_LEN];
    int len = uart_read_bytes(s_uart, buf, sizeof(buf), pdMS_TO_TICKS(BL0942_READ_TIMEOUT_MS));
    if (len != BL0942_PACKET_LEN) {
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] != BL0942_PACKET_HEADER) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t sum = cmd_byte;
    for (int i = 0; i < BL0942_PACKET_LEN - 1; i++) {
        sum += buf[i];
    }
    if ((uint8_t)~sum != buf[BL0942_PACKET_LEN - 1]) {
        return ESP_ERR_INVALID_CRC;
    }

    out->i_rms = le24(&buf[1]);
    out->v_rms = le24(&buf[4]);
    out->watt = le24_signed(&buf[10]);
    out->cf_cnt = le24(&buf[13]);
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
    s_pulses_per_kwh = energy_ref;

    const uart_config_t uart_cfg = {
        .baud_rate = CONFIG_BL0942_UART_BAUD,
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

    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d %d baud", s_uart, CONFIG_BL0942_UART_RX_GPIO, CONFIG_BL0942_UART_TX_GPIO,
             CONFIG_BL0942_UART_BAUD);
    bl0942_setup();
    return ESP_OK;
}

void bl0942_update(bl0942_reading_t *out)
{
    bl0942_raw_t raw;
    if (bl0942_read_raw(&raw) != ESP_OK) {
        /* Keep the last values for a few misses, then report the meter as unavailable */
        ++s_fail_count;
        if (s_fail_count == BL0942_MAX_MISSES) {
            ESP_LOGW(TAG, "BL0942 not responding");
        }
        if (s_fail_count % BL0942_SETUP_RETRY == 0) {
            bl0942_setup();
        }
        *out = s_last;
        if (s_fail_count >= BL0942_MAX_MISSES) {
            out->valid = false;
        }
        out->energy_mwh = s_energy_base_mwh + (uint64_t)s_energy_counted_mwh;
        return;
    }
    if (s_fail_count >= BL0942_MAX_MISSES || !s_last.valid) {
        ESP_LOGI(TAG, "BL0942 answering");
    }
    s_fail_count = 0;

    /* Energy from the 24-bit CF pulse counter. The plausibility limit grows with the time since the last good
     * read, so pulses counted while some reads failed are kept; a bigger jump means the BL0942 was reset. */
    const uint32_t cf = raw.cf_cnt & 0xFFFFFF;
    const int64_t now_us = esp_timer_get_time();
    if (s_last_cf >= 0) {
        const uint32_t delta = (cf - (uint32_t)s_last_cf) & 0xFFFFFF;
        const double hours = (now_us - s_last_cf_us) / 3.6e9;
        if (delta <= s_pulses_per_kwh * BL0942_MAX_POWER_KW * hours + 10) {
            s_energy_counted_mwh += delta * s_mwh_per_pulse;
        }
    }
    s_last_cf = (int32_t)cf;
    s_last_cf_us = now_us;

    float current = raw.i_rms / s_current_ref;
    /* The MODE register counts energy as an absolute value, so report power the same way: a reversed shunt or
     * CT wiring then still reads as consumption */
    float power = fabsf(raw.watt / s_power_ref);
    if (current < BL0942_CURRENT_NOISE_A) {
        current = 0;
    }
    if (power < BL0942_POWER_NOISE_W) {
        power = 0;
    }

    out->valid = true;
    out->voltage_v = raw.v_rms / s_voltage_ref;
    out->current_a = current;
    out->power_w = power;
    out->energy_mwh = s_energy_base_mwh + (uint64_t)s_energy_counted_mwh;
    s_last = *out;
}
