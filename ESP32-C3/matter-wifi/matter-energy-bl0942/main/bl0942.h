/*
 * BL0942 - IC đo điện năng 1 pha, giao tiếp UART.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <driver/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uart_port_t uart_port;
    int rx_gpio;          /* ESP32 RX <- TX của BL0942 */
    int tx_gpio;          /* ESP32 TX -> RX của BL0942 */
    int baud_rate;        /* 4800 (mặc định) hoặc 9600 */
    uint8_t address;      /* A1/A2: 0..3 */
} bl0942_config_t;

/* Giá trị thô đọc từ thanh ghi (gói "full electrical parameter", 23 byte) */
typedef struct {
    uint32_t i_rms;       /* 24-bit unsigned */
    uint32_t v_rms;       /* 24-bit unsigned */
    uint32_t i_fast_rms;  /* 24-bit unsigned */
    int32_t  watt;        /* 24-bit signed */
    uint32_t cf_cnt;      /* 24-bit unsigned, đếm xung năng lượng */
    uint16_t freq;        /* chu kỳ điện lưới, f = 1e6 / freq */
    uint32_t status;
} bl0942_raw_t;

esp_err_t bl0942_init(const bl0942_config_t *config);

/* Gửi lệnh đọc toàn bộ thông số và chờ gói trả về (kiểm tra checksum). */
esp_err_t bl0942_read_raw(bl0942_raw_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
