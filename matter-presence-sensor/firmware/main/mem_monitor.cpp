/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>
#include <stdio.h>

#include "mem_monitor.h"

static const char *TAG = "mem";

// Matter, Wi-Fi or OpenThread, the timers (LED, button, this log), the application tasks, and the serial console of
// the Matter shell when it is on
static const char *const kTasks[] = {
#if CONFIG_OPENTHREAD_ENABLED
    "CHIP", "ot_task", "esp_timer", "presence", "ld2410", "radar_cfg", "id_diag",
#else
    "CHIP", "wifi", "esp_timer", "presence", "ld2410", "radar_cfg", "id_diag",
#endif
#if CONFIG_ENABLE_CHIP_SHELL
    "console",
#endif
};

// Below these the device is close to failing allocations or overflowing a stack.
static constexpr size_t kLowHeapBytes = 16 * 1024;
static constexpr UBaseType_t kLowStackBytes = 512;

// Lowest free heap at the previous log, and when that log ran
static size_t s_prev_min = SIZE_MAX;
static int64_t s_prev_s;

static void log_memory(void *arg)
{
    const size_t free_now = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    const size_t free_min = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    const int64_t now_s = esp_timer_get_time() / 1000000;
    // The lowest since boot only goes down: warn once each time it reaches a new low below the limit, with the
    // time window it happened in.
    const bool new_low = free_min < kLowHeapBytes && free_min < s_prev_min;
    const int64_t since_s = s_prev_s;
    s_prev_min = free_min;
    s_prev_s = now_s;
    if (new_low) {
        ESP_LOGW(TAG, "Heap low: lowest %u bytes free since boot, reached between %lld s and %lld s; now %u free, "
                      "largest block %u", (unsigned)free_min, (long long)since_s, (long long)now_s,
                 (unsigned)free_now, (unsigned)largest);
    } else {
        ESP_LOGI(TAG, "Heap: %u bytes free, lowest %u since boot, largest block %u", (unsigned)free_now,
                 (unsigned)free_min, (unsigned)largest);
    }

    // Unused stack in bytes (StackType_t is one byte on ESP-IDF), the lowest since the task started.
    char line[128] = "";
    size_t len = 0;
    bool low = false;
    for (const char *name : kTasks) {
        TaskHandle_t task = xTaskGetHandle(name);
        if (task == nullptr || len >= sizeof(line)) {
            continue;
        }
        const UBaseType_t unused = uxTaskGetStackHighWaterMark(task);
        low = low || unused < kLowStackBytes;
        len += snprintf(&line[len], sizeof(line) - len, " %s %u", name, (unsigned)unused);
    }
    if (low) {
        ESP_LOGW(TAG, "Stack low, unused bytes:%s", line);
    } else {
        ESP_LOGI(TAG, "Stack unused bytes:%s", line);
    }
}

esp_err_t mem_monitor_start(uint32_t period_s)
{
    log_memory(nullptr);
    if (period_s == 0) {
        return ESP_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = log_memory,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mem_monitor",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer;
    esp_err_t err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, (uint64_t)period_s * 1000 * 1000);
    }
    return err;
}
