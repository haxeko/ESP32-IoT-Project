#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_PIN_NO_CHANGE -1
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
static inline esp_err_t uart_driver_install(int, int, int, int, void *, int) { return ESP_OK; }
static inline esp_err_t uart_driver_delete(int) { return ESP_OK; }
static inline esp_err_t uart_param_config(int, const uart_config_t *) { return ESP_OK; }
static inline esp_err_t uart_set_pin(int, int, int, int, int) { return ESP_OK; }
static inline int uart_read_bytes(int, void *buf, uint32_t len, int timeout_ms) { return sim_uart_read(buf, len, timeout_ms); }
static inline int uart_write_bytes(int, const void *buf, size_t len) { return sim_uart_write(buf, len); }
