#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
#define UART_NUM_1 1
#define UART_PIN_NO_CHANGE -1
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
static inline esp_err_t uart_driver_install(int, int, int, int, void *, int) { return ESP_OK; }
static inline esp_err_t uart_driver_delete(int) { return ESP_OK; }
static inline esp_err_t uart_param_config(int, const uart_config_t *) { return ESP_OK; }
static inline esp_err_t uart_set_pin(int, int, int, int, int) { return ESP_OK; }
// The radar simulator feeds its bytes to ld2410_feed() itself: the reader task is not run.
static inline int uart_read_bytes(int, void *, uint32_t, uint32_t ticks)
{
    sim_advance(ticks);
    return 0;
}
static inline esp_err_t uart_flush_input(int) { return ESP_OK; }
static inline int uart_write_bytes(int, const void *buf, size_t len)
{
    if (g_sim.on_uart_write) {
        g_sim.on_uart_write((const uint8_t *)buf, len);
    }
    return (int)len;
}
