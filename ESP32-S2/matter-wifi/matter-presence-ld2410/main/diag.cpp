/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_core_dump.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"

static const char *TAG = "diag";

static constexpr char kNamespace[] = "diag";
static constexpr size_t kHistory = 8;

static uint32_t s_boot_count;
static uint8_t s_resets[kHistory]; // newest first, esp_reset_reason_t values
static char s_crash[160];

static const char *reason_name(uint8_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "unknown";
    }
}

void diag_record_boot(void)
{
    const uint8_t reason = (uint8_t)esp_reset_reason();

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        size_t len = sizeof(s_resets);
        nvs_get_blob(handle, "resets", s_resets, &len);
        nvs_get_u32(handle, "boots", &s_boot_count);
        memmove(&s_resets[1], &s_resets[0], kHistory - 1);
        s_resets[0] = reason;
        s_boot_count++;
        nvs_set_blob(handle, "resets", s_resets, sizeof(s_resets));
        nvs_set_u32(handle, "boots", s_boot_count);
        nvs_commit(handle);
        nvs_close(handle);
    } else {
        s_resets[0] = reason;
    }

    esp_core_dump_summary_t summary;
    if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&summary) == ESP_OK) {
        int n = snprintf(s_crash, sizeof(s_crash), "task %s, PC 0x%08lx, EXCCAUSE %lu, backtrace",
                         summary.exc_task, (unsigned long)summary.exc_pc,
                         (unsigned long)summary.ex_info.exc_cause);
        for (uint32_t i = 0; i < summary.exc_bt_info.depth && i < 6 && n > 0 && n < (int)sizeof(s_crash); i++) {
            n += snprintf(&s_crash[n], sizeof(s_crash) - n, " 0x%08lx", (unsigned long)summary.exc_bt_info.bt[i]);
        }
    }

    char report[512];
    diag_get_report(report, sizeof(report));
    if (reason == ESP_RST_PANIC || reason == ESP_RST_BROWNOUT || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        ESP_LOGE(TAG, "%s", report);
    } else {
        ESP_LOGI(TAG, "%s", report);
    }
}

bool diag_last_reset_was_brownout(void)
{
    return s_resets[0] == ESP_RST_BROWNOUT;
}

void diag_get_report(char *buf, size_t size)
{
    int n = snprintf(buf, size, "Boot #%lu. Last resets (newest first):", (unsigned long)s_boot_count);
    for (size_t i = 0; i < kHistory && s_resets[i] != 0 && n > 0 && (size_t)n < size; i++) {
        n += snprintf(&buf[n], size - n, "%s %s", i ? "," : "", reason_name(s_resets[i]));
    }
    if (s_crash[0] && n > 0 && (size_t)n < size) {
        snprintf(&buf[n], size - n, ". Last crash: %s", s_crash);
    }
}
