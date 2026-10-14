/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// HLK-LD2410 driver: reads the periodic target report frames that the radar
// streams at 256000 8N1 in its default (basic) reporting mode. The frame
// format is decoded by ld2410_parser.c.

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ld2410.h"

static const char *TAG = "ld2410";

// UART0 stays free for the console (UART or USB CDC), so the radar uses UART1,
// which can be routed to any GPIO through the GPIO matrix.
#define LD2410_UART_NUM         UART_NUM_1
#define LD2410_BAUD_RATE        256000
#define LD2410_RX_BUF_SIZE      256
#define LD2410_TASK_STACK       2560
#define LD2410_TASK_PRIO        5

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ld2410_target_t s_target;
static bool s_has_target;

static void ld2410_task(void *arg)
{
    ld2410_parser_t parser;
    uint8_t rx[128];

    ld2410_parser_init(&parser);
    while (true) {
        const int len = uart_read_bytes(LD2410_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            ld2410_report_t report;
            if (!ld2410_parser_feed(&parser, rx[i], &report)) {
                continue;
            }
            const ld2410_target_t t = {
                .report = report,
                .timestamp_ms = esp_timer_get_time() / 1000,
            };
            taskENTER_CRITICAL(&s_lock);
            s_target = t;
            s_has_target = true;
            taskEXIT_CRITICAL(&s_lock);
        }
    }
}

bool ld2410_get_target(ld2410_target_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    const bool has_target = s_has_target;
    *out = s_target;
    taskEXIT_CRITICAL(&s_lock);
    return has_target;
}

esp_err_t ld2410_init(int rx_pin, int tx_pin)
{
    const uart_config_t uart_config = {
        .baud_rate = LD2410_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(LD2410_UART_NUM, LD2410_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(LD2410_UART_NUM, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(LD2410_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK &&
        xTaskCreate(ld2410_task, "ld2410", LD2410_TASK_STACK, NULL, LD2410_TASK_PRIO, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        uart_driver_delete(LD2410_UART_NUM);
        return err;
    }
    ESP_LOGI(TAG, "LD2410 on UART%d, RX=GPIO%d TX=GPIO%d", LD2410_UART_NUM, rx_pin, tx_pin);
    return ESP_OK;
}
