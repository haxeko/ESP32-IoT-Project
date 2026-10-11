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
#include <string.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <esp_timer.h>
#if __has_include(<hal/uart_periph.h>)
#include <hal/uart_periph.h>
#define BL0942_UART_RX_SIGNAL(port) UART_PERIPH_SIGNAL(port, SOC_UART_PERIPH_SIGNAL_RX)
#else
#include <soc/uart_periph.h>
#define BL0942_UART_RX_SIGNAL(port) UART_PERIPH_SIGNAL(port, SOC_UART_RX_PIN_IDX)
#endif
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
#define BL0942_REG_SOFT_RESET   0x1C
#define BL0942_REG_USR_WRPROT   0x1D
#define BL0942_SOFT_RESET_MAGIC 0x5A5A5A
#define BL0942_WRPROT_UNLOCK    0x55
/* MODE as set by ESPHome: RESV | CF_EN | CF_CNT_ADD_SEL (absolute energy) | RMS_UPDATE_SEL (800 ms), 50 Hz */
#define BL0942_MODE_VALUE       (0x03 | 0x04 | 0x80 | 0x08)

/* Readings below these are noise with no load connected */
#define BL0942_CURRENT_NOISE_A  0.015f
#define BL0942_POWER_NOISE_W    0.5f

static const uart_port_t s_uart = (uart_port_t)CONFIG_BL0942_UART_PORT;
static const uint8_t s_address = CONFIG_BL0942_ADDRESS;
static const int s_rx_gpio = CONFIG_BL0942_UART_RX_GPIO;
static const int s_tx_gpio = CONFIG_BL0942_UART_TX_GPIO;

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
static bool s_link_confirmed;
static int s_last_rx_len;
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

/* Returns the 24-bit register value, or -1 (no reply) / -2 (bad checksum) */
static int32_t read_reg(uint8_t reg)
{
    const uint8_t cmd[2] = { (uint8_t)(BL0942_READ_CMD | s_address), reg };
    uint8_t resp[4];
    uart_flush_input(s_uart);
    uart_write_bytes(s_uart, cmd, sizeof(cmd));
    int len = uart_read_bytes(s_uart, resp, sizeof(resp), pdMS_TO_TICKS(100));
    if (len != sizeof(resp)) {
        return -1;
    }
    const uint8_t sum = (uint8_t)((cmd[0] + reg + resp[0] + resp[1] + resp[2]) ^ 0xFF);
    if (sum != resp[3]) {
        return -2;
    }
    return (int32_t)le24(resp);
}

/*
 * Bring the BL0942 UART back to a known state, then configure it like ESPHome does.
 *
 * Bytes received at the wrong baud rate can leave the BL0942 waiting in the middle of a write frame; it then
 * ignores read commands until the frame is completed. Padding bytes (not a frame ID) complete any pending frame
 * (its checksum fails and it is dropped), then a soft reset restores the default register state.
 */
static void resync_and_setup(void)
{
    static const uint8_t pad[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uart_write_bytes(s_uart, pad, sizeof(pad));
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(20));

    write_reg(BL0942_REG_USR_WRPROT, BL0942_WRPROT_UNLOCK);
    write_reg(BL0942_REG_SOFT_RESET, BL0942_SOFT_RESET_MAGIC);
    vTaskDelay(pdMS_TO_TICKS(50));
    write_reg(BL0942_REG_USR_WRPROT, BL0942_WRPROT_UNLOCK);
    write_reg(BL0942_REG_MODE, BL0942_MODE_VALUE);
    write_reg(BL0942_REG_USR_WRPROT, 0);

    const int32_t mode = read_reg(BL0942_REG_MODE);
    if (mode == BL0942_MODE_VALUE) {
        ESP_LOGI(TAG, "BL0942 setup OK at %d baud (MODE=0x%06lx)", s_baud, (unsigned long)mode);
    } else if (mode >= 0) {
        ESP_LOGW(TAG, "BL0942 answered at %d baud but MODE=0x%06lx (expected 0x%06x)", s_baud, (unsigned long)mode,
                 BL0942_MODE_VALUE);
    } else {
        ESP_LOGW(TAG, "BL0942 setup: no %s reading MODE at %d baud", mode == -1 ? "reply" : "valid checksum",
                 s_baud);
    }
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
    s_last_rx_len = len < 0 ? 0 : len;
    if (len != BL0942_PACKET_LEN) {
        if (len > 0) {
            ESP_LOGW(TAG, "Short reply: %d/%d bytes", len, BL0942_PACKET_LEN);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_WARN);
        }
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] != BL0942_PACKET_HEADER) {
        ESP_LOGW(TAG, "Bad header 0x%02x", buf[0]);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_WARN);
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
    out->watt = le24_signed(&buf[10]);
    out->cf_cnt = le24(&buf[13]);
    out->freq = (uint16_t)(buf[16] | (buf[17] << 8));
    return ESP_OK;
}

/* Level of a pin with only a pull-down enabled: 1 means something drives it high.
 * The BL0942 TX pin idles high whenever the chip is powered. */
static int probe_line_level(int gpio, bool restore_pullup)
{
    gpio_set_pull_mode((gpio_num_t)gpio, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(2));
    int level = gpio_get_level((gpio_num_t)gpio);
    gpio_set_pull_mode((gpio_num_t)gpio, restore_pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
    return level;
}

static void log_line_check(void)
{
    /* Release the ESP TX pin for the measurement, otherwise we would read our own idle-high output */
    gpio_set_direction((gpio_num_t)s_tx_gpio, GPIO_MODE_INPUT);
    const int rx = probe_line_level(s_rx_gpio, true);
    const int tx = probe_line_level(s_tx_gpio, false);
    uart_set_pin(s_uart, s_tx_gpio, s_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "Line check (pull-down): GPIO%d (ESP RX)=%d, GPIO%d (ESP TX)=%d. The pin wired to BL0942 TX "
             "should read 1 when the BL0942 is powered; 0 on both means no power or no wire.",
             s_rx_gpio, rx, s_tx_gpio, tx);
}

/* Send two bytes that the BL0942 ignores (not a read/write frame ID) and return how many come back */
static int uart_echo(void)
{
    const uint8_t probe[2] = { 0x33, 0xCC };
    uint8_t buf[8] = { 0 };
    uart_flush_input(s_uart);
    uart_write_bytes(s_uart, probe, sizeof(probe));
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));
    int n = uart_read_bytes(s_uart, buf, sizeof(buf), pdMS_TO_TICKS(30));
    if (n < 0) {
        n = 0;
    }
    return (n >= 2 && buf[0] == probe[0] && buf[1] == probe[1]) ? n : -n;
}

/* Measure the real bit time on the TX pin: send 0x55 (alternating bits) and time the edges on the pad.
 * At 9600 baud a bit lasts 104 us, at 4800 baud 208 us. Returns the average bit time in us, or -1. */
static int measure_tx_bit_time_us(void)
{
    const uint8_t probe[4] = { 0x55, 0x55, 0x55, 0x55 };
    gpio_input_enable((gpio_num_t)s_tx_gpio);
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));

    int64_t first = 0, last_edge = 0;
    int edges = 0;
    int level = gpio_get_level((gpio_num_t)s_tx_gpio);
    uart_write_bytes(s_uart, probe, sizeof(probe));
    const int64_t end = esp_timer_get_time() + 20000; /* 4 bytes at 4800 baud take 8.3 ms */
    while (esp_timer_get_time() < end) {
        const int l = gpio_get_level((gpio_num_t)s_tx_gpio);
        if (l != level) {
            const int64_t now = esp_timer_get_time();
            if (edges == 0) {
                first = now;
            }
            last_edge = now;
            edges++;
            level = l;
        }
    }
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));
    uart_flush_input(s_uart);
    /* 0x55 framed 8N1 is 0101010101: every bit toggles, so edges - 1 intervals of one bit each */
    return edges > 1 ? (int)((last_edge - first) / (edges - 1)) : -1;
}

/* Diagnose the UART link step by step, logging one line:
 *  1. internal loopback: UART peripheral + driver work
 *  2. TX pad loopback (TX GPIO looped to UART RX inside the chip): bytes really leave on the TX pin
 *  3. after a real read command, count level changes on the RX pin: the BL0942 answers electrically */
static void self_test(void)
{
    uart_set_loop_back(s_uart, true);
    const int internal = uart_echo();
    uart_set_loop_back(s_uart, false);

    gpio_input_enable((gpio_num_t)s_tx_gpio);
    esp_rom_gpio_connect_in_signal(s_tx_gpio, BL0942_UART_RX_SIGNAL(s_uart), false);
    const int pad = uart_echo();
    uart_set_pin(s_uart, s_tx_gpio, s_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    const uint8_t cmd[2] = { (uint8_t)(BL0942_READ_CMD | s_address), BL0942_FULL_PACKET };
    uart_flush_input(s_uart);
    uart_write_bytes(s_uart, cmd, sizeof(cmd));
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(50));
    int last = gpio_get_level((gpio_num_t)s_rx_gpio);
    int edges = 0;
    const int64_t end = esp_timer_get_time() + 80000; /* 23 bytes at 4800 baud take 48 ms */
    while (esp_timer_get_time() < end) {
        const int level = gpio_get_level((gpio_num_t)s_rx_gpio);
        if (level != last) {
            edges++;
            last = level;
        }
    }
    uint8_t buf[BL0942_PACKET_LEN];
    int got = uart_read_bytes(s_uart, buf, sizeof(buf), 0);
    if (got < 0) {
        got = 0;
    }

    const int bit_us = measure_tx_bit_time_us();

    ESP_LOGI(TAG, "Self-test (RX=GPIO%d TX=GPIO%d %d baud addr %d): internal loopback %s, TX pin loopback %s, "
             "RX pin edges after read command: %d, UART bytes: %d, TX bit time %d us (expected %d us)",
             s_rx_gpio, s_tx_gpio, s_baud, s_address, internal > 0 ? "OK" : "FAIL", pad > 0 ? "OK" : "FAIL", edges,
             got, bit_us, 1000000 / s_baud);
    if (got > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, got, ESP_LOG_INFO);
    }
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
        err = uart_set_pin(s_uart, s_tx_gpio, s_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        uart_driver_delete(s_uart);
        return err;
    }

    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d %d baud addr %d, Vref=%.2f Iref=%.2f Pref=%.3f Eref=%.2f pulse/kWh",
             s_uart, s_rx_gpio, s_tx_gpio, s_baud, s_address, s_voltage_ref, s_current_ref, s_power_ref, energy_ref);
    log_line_check();
    resync_and_setup();
    self_test();
    return ESP_OK;
}

void bl0942_update(bl0942_reading_t *out)
{
    bl0942_raw_t raw;
    if (bl0942_read_raw(&raw) != ESP_OK) {
        /* Keep the last values for a few misses, then report the meter as unavailable */
        ++s_fail_count;
        if (s_fail_count % 3 == 0) {
            ESP_LOGW(TAG, "No valid reply (%d baud, %d bytes received)", s_baud, s_last_rx_len);
            /* Until the link works, alternate between the two baud rates selectable by the BL0942 SCLK_BPS pin */
            if (!s_link_confirmed) {
                s_baud = (s_baud == 4800) ? 9600 : 4800;
                uart_set_baudrate(s_uart, s_baud);
            }
            resync_and_setup();
            self_test();
        }
        *out = s_last;
        if (s_fail_count >= 3) {
            out->valid = false;
        }
        out->energy_mwh = s_energy_base_mwh + (uint64_t)s_energy_counted_mwh;
        return;
    }
    s_fail_count = 0;
    if (!s_link_confirmed) {
        s_link_confirmed = true;
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
