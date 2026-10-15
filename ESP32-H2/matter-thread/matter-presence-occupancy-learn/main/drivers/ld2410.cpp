/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// HLK-LD2410 driver (serial protocol V1.0x, 256000 8N1).
//
// The radar streams target report frames:
//   F4 F3 F2 F1 | LEN (2, LE) | TYPE AA STATE MOV_CM(2) MOV_E STILL_CM(2) STILL_E DET_CM(2) [engineering data] 55 00 | F8 F7 F6 F5
// and answers configuration commands with ACK frames:
//   FD FC FB FA | LEN (2, LE) | COMMAND|0x0100 (2) STATUS (2) [return value] | 04 03 02 01
// Commands are sent in the same format as the ACKs, without the status.

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
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
#define LD2410_MAX_CMD_VALUE    18
#define LD2410_ACK_TIMEOUT_MS   1000
#define LD2410_MAX_ACK_DATA     40

static const uint8_t k_data_header[] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t k_data_footer[] = { 0xF8, 0xF7, 0xF6, 0xF5 };
static const uint8_t k_cmd_header[] = { 0xFD, 0xFC, 0xFB, 0xFA };
static const uint8_t k_cmd_footer[] = { 0x04, 0x03, 0x02, 0x01 };

// Command words
static constexpr uint16_t kCmdEnableConfig = 0x00FF;
static constexpr uint16_t kCmdEndConfig = 0x00FE;
static constexpr uint16_t kCmdMaxGatesDuration = 0x0060;
static constexpr uint16_t kCmdReadParams = 0x0061;
static constexpr uint16_t kCmdEngineeringOn = 0x0062;
static constexpr uint16_t kCmdEngineeringOff = 0x0063;
static constexpr uint16_t kCmdGateSensitivity = 0x0064;
static constexpr uint16_t kCmdReadResolution = 0x00AB;
static constexpr uint16_t kAckFlag = 0x0100;

// Commands are retried: the radar sometimes misses a request while it is
// sending a report, and a radar left in configuration mode stops reporting.
static constexpr int kConfigModeAttempts = 3;

typedef struct {
    uint16_t command; // command word | kAckFlag
    uint16_t status;  // 0 = success
    uint8_t len;
    uint8_t data[LD2410_MAX_ACK_DATA]; // return value after the status
} ld2410_ack_t;

// Frame parser, only used by the reader task.
typedef struct {
    const uint8_t *header; // k_data_header or k_cmd_header once the first byte matched
    const uint8_t *footer;
    size_t pos;
    size_t frame_len;
    uint8_t buf[sizeof(k_data_header) + 2 + LD2410_MAX_PAYLOAD + sizeof(k_data_footer)];
} ld2410_parser_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ld2410_target_t s_target;
static bool s_has_target;
static bool s_capturing;
static ld2410_capture_t s_capture;

static QueueHandle_t s_ack_queue;
static SemaphoreHandle_t s_cmd_mutex;

static void handle_target_payload(const uint8_t *p, size_t len)
{
    // TYPE (0x01 engineering / 0x02 basic), 0xAA head, the basic target data
    // (11 bytes from TYPE), engineering data if any, then the 0x55 tail and a
    // check byte (0x00). The tail rejects frames corrupted by line noise.
    if (len < 13 || (p[0] != 0x01 && p[0] != 0x02) || p[1] != 0xAA || p[len - 2] != 0x55 || p[len - 1] != 0x00) {
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

    // Engineering data: highest moving gate N, highest still gate M, moving
    // energy of gates 0..N, still energy of gates 0..M, then extra bytes.
    const uint8_t *moving_energy = nullptr;
    const uint8_t *still_energy = nullptr;
    size_t moving_gates = 0;
    size_t still_gates = 0;
    if (p[0] == 0x01 && len >= 13 + 2) {
        moving_gates = (size_t)p[11] + 1;
        still_gates = (size_t)p[12] + 1;
        if (moving_gates <= LD2410_GATE_COUNT && still_gates <= LD2410_GATE_COUNT &&
            13 + moving_gates + still_gates <= len - 2) {
            moving_energy = &p[13];
            still_energy = &p[13 + moving_gates];
        }
    }

    taskENTER_CRITICAL(&s_lock);
    s_target = t;
    s_has_target = true;
    if (s_capturing && moving_energy) {
        for (size_t g = 0; g < moving_gates; g++) {
            if (moving_energy[g] > s_capture.moving[g]) {
                s_capture.moving[g] = moving_energy[g];
            }
        }
        for (size_t g = 0; g < still_gates; g++) {
            if (still_energy[g] > s_capture.still[g]) {
                s_capture.still[g] = still_energy[g];
            }
        }
        s_capture.frames++;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void handle_ack_payload(const uint8_t *p, size_t len)
{
    if (len < 4 || !s_ack_queue) {
        return;
    }
    ld2410_ack_t ack = {};
    ack.command = (uint16_t)(p[0] | (p[1] << 8));
    ack.status = (uint16_t)(p[2] | (p[3] << 8));
    ack.len = (uint8_t)(len - 4 < sizeof(ack.data) ? len - 4 : sizeof(ack.data));
    memcpy(ack.data, &p[4], ack.len);
    // Dropped when nobody waits for it (queue full)
    xQueueSend(s_ack_queue, &ack, 0);
}

static void parser_feed(ld2410_parser_t *ps, uint8_t b)
{
    if (ps->pos == 0) {
        if (b == k_data_header[0]) {
            ps->header = k_data_header;
            ps->footer = k_data_footer;
        } else if (b == k_cmd_header[0]) {
            ps->header = k_cmd_header;
            ps->footer = k_cmd_footer;
        } else {
            return;
        }
        ps->buf[ps->pos++] = b;
        return;
    }
    if (ps->pos < sizeof(k_data_header)) {
        if (b != ps->header[ps->pos]) {
            // Not a header after all, but this byte may start the next one.
            ps->pos = 0;
            parser_feed(ps, b);
            return;
        }
        ps->buf[ps->pos++] = b;
        return;
    }

    ps->buf[ps->pos++] = b;
    if (ps->pos == sizeof(k_data_header) + 2) {
        size_t payload_len = ps->buf[4] | (ps->buf[5] << 8);
        if (payload_len > LD2410_MAX_PAYLOAD) {
            ps->pos = 0;
            return;
        }
        ps->frame_len = sizeof(k_data_header) + 2 + payload_len + sizeof(k_data_footer);
    }
    if (ps->pos < sizeof(k_data_header) + 2 || ps->pos < ps->frame_len) {
        return;
    }
    if (memcmp(&ps->buf[ps->frame_len - sizeof(k_data_footer)], ps->footer, sizeof(k_data_footer)) == 0) {
        const uint8_t *payload = &ps->buf[sizeof(k_data_header) + 2];
        const size_t payload_len = ps->frame_len - sizeof(k_data_header) - 2 - sizeof(k_data_footer);
        if (ps->header == k_data_header) {
            handle_target_payload(payload, payload_len);
        } else {
            handle_ack_payload(payload, payload_len);
        }
    }
    ps->pos = 0;
}

static void ld2410_task(void *arg)
{
    static ld2410_parser_t s_parser;
    uint8_t rx[128];

    while (true) {
        int len = uart_read_bytes(LD2410_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            parser_feed(&s_parser, rx[i]);
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

// Sends one command and waits for its ACK. ack_out (optional) receives the
// return value. Caller holds s_cmd_mutex.
static esp_err_t send_command(uint16_t command, const uint8_t *value, size_t value_len, ld2410_ack_t *ack_out)
{
    if (value_len > LD2410_MAX_CMD_VALUE) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t frame[sizeof(k_cmd_header) + 2 + 2 + LD2410_MAX_CMD_VALUE + sizeof(k_cmd_footer)];
    size_t n = 0;
    memcpy(&frame[n], k_cmd_header, sizeof(k_cmd_header));
    n += sizeof(k_cmd_header);
    const size_t data_len = 2 + value_len;
    frame[n++] = (uint8_t)(data_len & 0xFF);
    frame[n++] = (uint8_t)(data_len >> 8);
    frame[n++] = (uint8_t)(command & 0xFF);
    frame[n++] = (uint8_t)(command >> 8);
    if (value_len > 0) {
        memcpy(&frame[n], value, value_len);
        n += value_len;
    }
    memcpy(&frame[n], k_cmd_footer, sizeof(k_cmd_footer));
    n += sizeof(k_cmd_footer);

    xQueueReset(s_ack_queue);
    if (uart_write_bytes(LD2410_UART_NUM, frame, n) != (int)n) {
        return ESP_FAIL;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(LD2410_ACK_TIMEOUT_MS);
    ld2410_ack_t ack;
    for (;;) {
        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout || xQueueReceive(s_ack_queue, &ack, timeout - elapsed) != pdTRUE) {
            ESP_LOGW(TAG, "No ACK for command 0x%04X", command);
            return ESP_ERR_TIMEOUT;
        }
        if (ack.command != (command | kAckFlag)) {
            continue; // late ACK of an earlier command
        }
        if (ack.status != 0) {
            ESP_LOGW(TAG, "Command 0x%04X rejected, status %u", command, ack.status);
            return ESP_FAIL;
        }
        if (ack_out) {
            *ack_out = ack;
        }
        return ESP_OK;
    }
}

static esp_err_t command(uint16_t cmd, const uint8_t *value, size_t value_len, ld2410_ack_t *ack_out)
{
    if (!s_cmd_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_command(cmd, value, value_len, ack_out);
    xSemaphoreGive(s_cmd_mutex);
    return err;
}

esp_err_t ld2410_config_begin(void)
{
    static const uint8_t value[] = { 0x01, 0x00 };
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < kConfigModeAttempts && err != ESP_OK; attempt++) {
        err = command(kCmdEnableConfig, value, sizeof(value), NULL);
    }
    return err;
}

esp_err_t ld2410_config_end(void)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < kConfigModeAttempts && err != ESP_OK; attempt++) {
        err = command(kCmdEndConfig, NULL, 0, NULL);
    }
    return err;
}

esp_err_t ld2410_set_engineering_mode(bool enable)
{
    return command(enable ? kCmdEngineeringOn : kCmdEngineeringOff, NULL, 0, NULL);
}

esp_err_t ld2410_read_config(ld2410_config_t *out)
{
    // Return value: 0xAA, highest gate N (8), max moving gate, max still gate,
    // moving thresholds of gates 0..N, still thresholds of gates 0..N, no-one duration (2).
    ld2410_ack_t ack;
    esp_err_t err = command(kCmdReadParams, NULL, 0, &ack);
    if (err != ESP_OK) {
        return err;
    }
    const size_t gates = (size_t)ack.data[1] + 1;
    const size_t duration_pos = 4 + 2 * LD2410_GATE_COUNT;
    if (ack.len < duration_pos + 2 || ack.data[0] != 0xAA || gates != LD2410_GATE_COUNT) {
        ESP_LOGW(TAG, "Unexpected parameter reply (%u bytes)", ack.len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->max_moving_gate = ack.data[2];
    out->max_still_gate = ack.data[3];
    memcpy(out->thresholds.moving, &ack.data[4], LD2410_GATE_COUNT);
    memcpy(out->thresholds.still, &ack.data[4 + LD2410_GATE_COUNT], LD2410_GATE_COUNT);
    out->no_one_duration_s = (uint16_t)(ack.data[duration_pos] | (ack.data[duration_pos + 1] << 8));
    return ESP_OK;
}

esp_err_t ld2410_read_thresholds(ld2410_thresholds_t *out)
{
    ld2410_config_t config;
    esp_err_t err = ld2410_read_config(&config);
    if (err == ESP_OK) {
        *out = config.thresholds;
    }
    return err;
}

esp_err_t ld2410_set_gate_thresholds(uint8_t gate, uint8_t moving, uint8_t still)
{
    if (gate >= LD2410_GATE_COUNT || moving > 100 || still > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    // Gate word 0x0000 + gate (4), moving word 0x0001 + value (4), still word 0x0002 + value (4)
    const uint8_t value[LD2410_MAX_CMD_VALUE] = {
        0x00, 0x00, gate,   0x00, 0x00, 0x00,
        0x01, 0x00, moving, 0x00, 0x00, 0x00,
        0x02, 0x00, still,  0x00, 0x00, 0x00,
    };
    return command(kCmdGateSensitivity, value, sizeof(value), NULL);
}

esp_err_t ld2410_set_max_gates_and_duration(uint8_t max_moving_gate, uint8_t max_still_gate,
                                            uint16_t no_one_duration_s)
{
    if (max_moving_gate < LD2410_MIN_MAX_GATE || max_moving_gate > LD2410_MAX_MAX_GATE ||
        max_still_gate < LD2410_MIN_MAX_GATE || max_still_gate > LD2410_MAX_MAX_GATE) {
        return ESP_ERR_INVALID_ARG;
    }
    // Max moving gate word 0x0000 + value (4), max still gate word 0x0001 +
    // value (4), no-one duration word 0x0002 + value (4)
    const uint8_t value[LD2410_MAX_CMD_VALUE] = {
        0x00, 0x00, max_moving_gate, 0x00, 0x00, 0x00,
        0x01, 0x00, max_still_gate,  0x00, 0x00, 0x00,
        0x02, 0x00, (uint8_t)(no_one_duration_s & 0xFF), (uint8_t)(no_one_duration_s >> 8), 0x00, 0x00,
    };
    return command(kCmdMaxGatesDuration, value, sizeof(value), NULL);
}

esp_err_t ld2410_read_gate_length(uint16_t *gate_cm)
{
    // Return value: resolution (2): 0 = 0.75 m, 1 = 0.2 m
    ld2410_ack_t ack;
    esp_err_t err = command(kCmdReadResolution, NULL, 0, &ack);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t resolution = ack.len >= 2 ? (uint16_t)(ack.data[0] | (ack.data[1] << 8)) : 0xFFFF;
    if (resolution == 0) {
        *gate_cm = 75;
    } else if (resolution == 1) {
        *gate_cm = 20;
    } else {
        ESP_LOGW(TAG, "Unknown distance resolution %u", resolution);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

void ld2410_capture_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    memset(&s_capture, 0, sizeof(s_capture));
    s_capturing = true;
    taskEXIT_CRITICAL(&s_lock);
}

void ld2410_capture_stop(ld2410_capture_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    s_capturing = false;
    *out = s_capture;
    taskEXIT_CRITICAL(&s_lock);
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
    s_ack_queue = xQueueCreate(2, sizeof(ld2410_ack_t));
    s_cmd_mutex = xSemaphoreCreateMutex();
    if (!s_ack_queue || !s_cmd_mutex) {
        return ESP_ERR_NO_MEM;
    }
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
