/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Diagnostics to find why commissioning or a sensor fails (CONFIG_APP_DIAG):
// - resets counted by reason, kept in NVS, so a reboot loop or a brown-out
//   shows even when no console was attached when it happened;
// - each commissioning event logged with the heap, to see where it stops;
// - a periodic status: heap, stack headroom and CPU share of every task,
//   sensor and OLED transfer counters, Wi-Fi signal.

#include <esp_system.h>

#include "app_diag.h"

const char *app_diag_reset_reason(void)
{
    // esp_reset_reason_t values, in their order.
    static const char *const k_names[] = {
        "unknown", "power-on", "external pin", "software", "panic", "interrupt watchdog", "task watchdog",
        "other watchdog", "deep sleep", "brown-out", "SDIO", "USB", "JTAG", "eFuse error", "power glitch",
        "CPU lock-up",
    };
    const unsigned reason = (unsigned)esp_reset_reason();
    return reason < sizeof(k_names) / sizeof(k_names[0]) ? k_names[reason] : "?";
}

#if CONFIG_APP_DIAG

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#include "drivers/pm_sensor.h"
#include "drivers/scd4x.h"
#include "local_ui.h"

static const char *TAG = "app_diag";

#define DIAG_TASK_STACK     3584
// Above the Matter thread, so that a busy Matter thread shows in the log
// instead of delaying it; below the sensor and UI tasks.
#define DIAG_TASK_PRIO      2
#define DIAG_MAX_TASKS      24

static constexpr char kNvsNamespace[] = "app_diag";
static constexpr char kNvsResetsKey[] = "resets";

// Resets since the counters were cleared, by reason.
typedef struct {
    uint32_t total;
    uint16_t power_on;
    uint16_t brownout;      // brown-out detector, power glitch
    uint16_t watchdog;      // task, interrupt and other watchdogs
    uint16_t panic;         // exception, abort, stack overflow, CPU lock-up
    uint16_t software;      // esp_restart(): factory reset, OTA
    uint16_t other;         // reset pin, USB, JTAG...
} resets_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static resets_t s_resets;       // under s_lock
static int s_stage = APP_DIAG_STAGE_BOOT;   // furthest stage since boot; written by the Matter thread

static void save_resets(const resets_t &resets)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kNvsResetsKey, &resets, sizeof(resets));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save the reset counters: %s", esp_err_to_name(err));
    }
}

static void count_reset(uint16_t *counter)
{
    if (*counter < UINT16_MAX) {
        (*counter)++;
    }
}

static void log_resets(const resets_t &r)
{
    ESP_LOGI(TAG, "resets: total=%u power-on=%u brown-out=%u watchdog=%u panic=%u software=%u other=%u | last: %s",
             (unsigned)r.total, r.power_on, r.brownout, r.watchdog, r.panic, r.software, r.other,
             app_diag_reset_reason());
}

void app_diag_init(void)
{
    resets_t resets = {};
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(resets);
        if (nvs_get_blob(handle, kNvsResetsKey, &resets, &size) != ESP_OK || size != sizeof(resets)) {
            resets = {};
        }
        nvs_close(handle);
    }

    resets.total++;
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        count_reset(&resets.power_on);
        break;
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH:
        count_reset(&resets.brownout);
        break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        count_reset(&resets.watchdog);
        break;
    case ESP_RST_PANIC:
    case ESP_RST_CPU_LOCKUP:
        count_reset(&resets.panic);
        break;
    case ESP_RST_SW:
        count_reset(&resets.software);
        break;
    default:
        count_reset(&resets.other);
        break;
    }
    save_resets(resets);

    taskENTER_CRITICAL(&s_lock);
    s_resets = resets;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "Diagnostics build: extra logs, reset counters in NVS");
    log_resets(resets);
}

void app_diag_clear(void)
{
    const resets_t resets = {};
    taskENTER_CRITICAL(&s_lock);
    s_resets = resets;
    taskEXIT_CRITICAL(&s_lock);
    save_resets(resets);
    ESP_LOGI(TAG, "Reset counters cleared");
}

void app_diag_event(const char *what, app_diag_stage_t stage)
{
    if (stage > s_stage) {
        s_stage = stage;
    }
    ESP_LOGI(TAG, "EVENT %s | stage=%d heap free=%u min=%u", what, s_stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

void app_diag_status_line(char *buf, size_t size)
{
    taskENTER_CRITICAL(&s_lock);
    const resets_t r = s_resets;
    taskEXIT_CRITICAL(&s_lock);
    // Two digits at most, to fit the width of the OLED.
    const auto two = [](unsigned v) { return v > 99 ? 99u : v; };
    snprintf(buf, size, "N%u B%u W%u P%u S%d", two(r.total), two(r.brownout), two(r.watchdog), two(r.panic),
             s_stage);
}

// Stack headroom (bytes, lowest since the task started) and CPU share since
// the previous call, for every task.
static void log_tasks()
{
    static TaskStatus_t s_tasks[DIAG_MAX_TASKS];
    static struct {
        TaskHandle_t handle;
        configRUN_TIME_COUNTER_TYPE run_time;
    } s_previous[DIAG_MAX_TASKS];
    static configRUN_TIME_COUNTER_TYPE s_previous_total;

    configRUN_TIME_COUNTER_TYPE total = 0;
    const UBaseType_t n = uxTaskGetSystemState(s_tasks, DIAG_MAX_TASKS, &total);
    if (n == 0) {
        ESP_LOGW(TAG, "tasks: more than %d", DIAG_MAX_TASKS);
        return;
    }
    const configRUN_TIME_COUNTER_TYPE elapsed = total - s_previous_total;

    char line[128];
    size_t len = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        configRUN_TIME_COUNTER_TYPE before = 0;
        for (const auto &p : s_previous) {
            if (p.handle == s_tasks[i].xHandle) {
                before = p.run_time;
                break;
            }
        }
        // Tenths of a percent; the counters wrap (after 71 minutes), their difference does not.
        const unsigned cpu = elapsed > 0 ? (unsigned)((uint64_t)(s_tasks[i].ulRunTimeCounter - before) * 1000 / elapsed)
                                         : 0;
        len += snprintf(line + len, sizeof(line) - len, " %s:%u/%u.%u", s_tasks[i].pcTaskName,
                        (unsigned)s_tasks[i].usStackHighWaterMark, cpu / 10, cpu % 10);
        if (len > sizeof(line) - 32 || i == n - 1) {
            ESP_LOGI(TAG, "tasks (name:stack free/cpu%%):%s", line);
            len = 0;
        }
    }
    for (UBaseType_t i = 0; i < DIAG_MAX_TASKS; i++) {
        s_previous[i].handle = i < n ? s_tasks[i].xHandle : NULL;
        s_previous[i].run_time = i < n ? s_tasks[i].ulRunTimeCounter : 0;
    }
    s_previous_total = total;
}

static void app_diag_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_DIAG_LOG_INTERVAL_SEC * 1000));

        taskENTER_CRITICAL(&s_lock);
        const resets_t resets = s_resets;
        taskEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "---- up %u s, stage %d ----", (unsigned)(esp_timer_get_time() / 1000000), s_stage);
        log_resets(resets);
        ESP_LOGI(TAG, "heap: free=%u min=%u largest=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        log_tasks();

#if !CONFIG_APP_DIAG_DISABLE_PM_SENSOR
        pm_sensor_stats_t pm;
        pm_sensor_get_stats(&pm);
        ESP_LOGI(TAG, "pm: rx=%u frames=%u checksum_err=%u other=%u dropped=%u gap_max=%u ms reports=%u invalid=%u "
                 "wake=%u sleep_retry=%u",
                 (unsigned)pm.rx_bytes, (unsigned)pm.frames, (unsigned)pm.checksum_errors, (unsigned)pm.other_frames,
                 (unsigned)pm.dropped_bytes, (unsigned)pm.max_frame_gap_ms, (unsigned)pm.reports,
                 (unsigned)pm.invalid_reports, (unsigned)pm.wakeups, (unsigned)pm.sleep_retries);
#endif
#if CONFIG_CO2_SENSOR_SCD4X && !CONFIG_APP_DIAG_DISABLE_CO2_SENSOR
        scd4x_stats_t co2;
        scd4x_get_stats(&co2);
        ESP_LOGI(TAG, "co2: xfer=%u fail=%u max=%u us crc_err=%u meas=%u starts=%u start_fail=%u reports=%u "
                 "invalid=%u",
                 (unsigned)co2.transfers, (unsigned)co2.failed_transfers, (unsigned)co2.max_transfer_us,
                 (unsigned)co2.crc_errors, (unsigned)co2.measurements, (unsigned)co2.starts,
                 (unsigned)co2.failed_starts, (unsigned)co2.reports, (unsigned)co2.invalid_reports);
#endif
        local_ui_stats_t ui;
        local_ui_get_stats(&ui);
        ESP_LOGI(TAG, "oled: frames=%u write_err=%u inits=%u init_fail=%u frame_max=%u ms", (unsigned)ui.oled_frames,
                 (unsigned)ui.oled_write_errors, (unsigned)ui.oled_inits, (unsigned)ui.oled_init_failures,
                 (unsigned)ui.oled_max_frame_ms);

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "wifi: connected, rssi=%d dBm, channel %d", ap.rssi, ap.primary);
        } else {
            ESP_LOGI(TAG, "wifi: not connected");
        }
    }
}

void app_diag_start(void)
{
    if (xTaskCreate(app_diag_task, "app_diag", DIAG_TASK_STACK, NULL, DIAG_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start the status log");
    }
}

#endif // CONFIG_APP_DIAG
