#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/queue.h"
#include "sim_probe.h"
typedef int uart_port_t;
#define UART_PIN_NO_CHANGE -1
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, rx_flow_ctrl_thresh, source_clk;
    uint32_t flags;
} uart_config_t;
typedef enum { UART_DATA, UART_BREAK, UART_BUFFER_FULL, UART_FIFO_OVF, UART_FRAME_ERR, UART_PARITY_ERR } uart_event_type_t;
typedef struct {
    uart_event_type_t type;
    size_t size;
    bool timeout_flag;
} uart_event_t;
esp_err_t uart_driver_install(uart_port_t port, int rx_size, int tx_size, int queue_len, QueueHandle_t *queue, int flags);
static inline esp_err_t uart_param_config(uart_port_t port, const uart_config_t *c)
{
    g_sim.port[port].baud = (uint32_t)c->baud_rate;
    return ESP_OK;
}
static inline esp_err_t uart_set_pin(uart_port_t, int, int, int, int) { return ESP_OK; }
static inline esp_err_t uart_set_rx_timeout(uart_port_t, uint8_t) { return ESP_OK; }
static inline esp_err_t uart_set_baudrate(uart_port_t port, uint32_t baud)
{
    g_sim.port[port].baud = baud;
    return ESP_OK;
}
static inline esp_err_t uart_flush_input(uart_port_t port)
{
    g_sim.port[port].rx.clear();
    return ESP_OK;
}
int uart_read_bytes(uart_port_t port, void *buf, uint32_t len, TickType_t ticks);
int uart_write_bytes(uart_port_t port, const void *data, size_t len);
