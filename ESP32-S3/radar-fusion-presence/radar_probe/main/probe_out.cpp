/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "probe_out.h"

// Enough for the longest frame (74 bytes = 148 hex digits) with its fields
#define RECORD_MAX 400
// About 10 s of records at the normal rate (both radars, engineering mode)
#define BUFFER_SIZE (32 * 1024)
#define OUT_TASK_STACK 3072
#define OUT_TASK_PRIO 3

static RingbufHandle_t s_buffer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped;

// line[0] is '@'; appends "*<checksum>\n" and queues the line
static void queue_line(char *line, size_t len)
{
    uint8_t checksum = 0;
    for (size_t i = 1; i < len; i++) {
        checksum ^= (uint8_t)line[i];
    }
    len += (size_t)snprintf(&line[len], RECORD_MAX - len, "*%02X\n", checksum);
    if (!s_buffer || xRingbufferSend(s_buffer, line, len, 0) != pdTRUE) {
        taskENTER_CRITICAL(&s_lock);
        s_dropped++;
        taskEXIT_CRITICAL(&s_lock);
    }
}

void probe_out(char type, const char *fmt, ...)
{
    char line[RECORD_MAX];
    const int head = snprintf(line, sizeof(line), "@%c,", type);
    va_list args;
    va_start(args, fmt);
    const int body = vsnprintf(&line[head], sizeof(line) - head - 4, fmt, args);
    va_end(args);
    if (body < 0) {
        return;
    }
    size_t len = (size_t)head + (size_t)body;
    if (len > sizeof(line) - 5) {
        len = sizeof(line) - 5; // truncated: still a valid record
    }
    queue_line(line, len);
}

void probe_out_hex(char type, const char *head, const uint8_t *data, size_t len)
{
    static const char k_digits[] = "0123456789ABCDEF";
    char line[RECORD_MAX];
    size_t n = (size_t)snprintf(line, sizeof(line), "@%c,%s,", type, head);
    for (size_t i = 0; i < len && n + 2 < sizeof(line) - 5; i++) {
        line[n++] = k_digits[data[i] >> 4];
        line[n++] = k_digits[data[i] & 0x0F];
    }
    queue_line(line, n);
}

uint32_t probe_out_dropped(void)
{
    taskENTER_CRITICAL(&s_lock);
    const uint32_t dropped = s_dropped;
    taskEXIT_CRITICAL(&s_lock);
    return dropped;
}

static void out_task(void *arg)
{
    for (;;) {
        size_t size = 0;
        char *item = (char *)xRingbufferReceive(s_buffer, &size, portMAX_DELAY);
        while (item) {
            fwrite(item, 1, size, stdout);
            vRingbufferReturnItem(s_buffer, item);
            item = (char *)xRingbufferReceive(s_buffer, &size, 0);
        }
        fflush(stdout);
    }
}

esp_err_t probe_out_init(void)
{
    s_buffer = xRingbufferCreate(BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!s_buffer) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(out_task, "probe_out", OUT_TASK_STACK, NULL, OUT_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
