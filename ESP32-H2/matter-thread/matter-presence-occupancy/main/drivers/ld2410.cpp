/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// HLK-LD2410 driver: parses the periodic target report frames that the radar
// streams at 256000 8N1 in its default (basic) reporting mode.
//
// Frame: F4 F3 F2 F1 | LEN (2, LE) | TYPE AA STATE MOV_CM(2) MOV_E STILL_CM(2) STILL_E DET_CM(2) ... 55 00 | F8 F7 F6 F5

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "ld2410.h"

static const char *TAG = "ld2410";

// UART0 stays free for the console (UART or USB CDC), so the radar uses UART1,
// which can be routed to any GPIO through the GPIO matrix.
#define LD2410_UART_NUM         UART_NUM_1
#define LD2410_BAUD_RATE        256000
#define LD2410_RX_BUF_SIZE      256
#define LD2410_TASK_STACK       2560
#define LD2410_TASK_PRIO        5
#define LD2410_MAX_PAYLOAD      64

static const uint8_t k_header[] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t k_footer[] = { 0xF8, 0xF7, 0xF6, 0xF5 };

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ld2410_target_t s_target;
static bool s_has_target;

static void handle_payload(const uint8_t *p, size_t len)
{
    // TYPE (0x01 engineering / 0x02 basic), 0xAA head, then the basic target data.
    if (len < 11 || (p[0] != 0x01 && p[0] != 0x02) || p[1] != 0xAA) {
        return;
    }
    ld2410_target_t t = {
        .moving = (p[2] & 0x01) != 0,
        .still = (p[2] & 0x02) != 0,
        .moving_cm = (uint16_t)(p[3] | (p[4] << 8)),
        .still_cm = (uint16_t)(p[6] | (p[7] << 8)),
        .detection_cm = (uint16_t)(p[9] | (p[10] << 8)),
        .timestamp_ms = esp_timer_get_time() / 1000,
    };
    taskENTER_CRITICAL(&s_lock);
    s_target = t;
    s_has_target = true;
    taskEXIT_CRITICAL(&s_lock);
}

static void ld2410_task(void *arg)
{
    uint8_t rx[128];
    uint8_t buf[sizeof(k_header) + 2 + LD2410_MAX_PAYLOAD + sizeof(k_footer)];
    size_t pos = 0;
    size_t frame_len = 0;

    while (true) {
        int len = uart_read_bytes(LD2410_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            uint8_t b = rx[i];
            if (pos < sizeof(k_header)) {
                if (b != k_header[pos]) {
                    pos = (b == k_header[0]) ? 1 : 0;
                    continue;
                }
                buf[pos++] = b;
                continue;
            }
            buf[pos++] = b;
            if (pos == sizeof(k_header) + 2) {
                size_t payload_len = buf[4] | (buf[5] << 8);
                if (payload_len > LD2410_MAX_PAYLOAD) {
                    pos = 0;
                    continue;
                }
                frame_len = sizeof(k_header) + 2 + payload_len + sizeof(k_footer);
            }
            if (pos < sizeof(k_header) + 2 || pos < frame_len) {
                continue;
            }
            if (memcmp(&buf[frame_len - sizeof(k_footer)], k_footer, sizeof(k_footer)) == 0) {
                handle_payload(&buf[sizeof(k_header) + 2], frame_len - sizeof(k_header) - 2 - sizeof(k_footer));
            }
            pos = 0;
        }
    }
}

bool ld2410_get_target(ld2410_target_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    bool has_target = s_has_target;
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
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(LD2410_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(ld2410_task, "ld2410", LD2410_TASK_STACK, NULL, LD2410_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LD2410 on UART%d, RX=GPIO%d TX=GPIO%d", LD2410_UART_NUM, rx_pin, tx_pin);
    return ESP_OK;
}
