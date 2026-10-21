/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "presence.h"

static const char *TAG = "presence";

static constexpr uint32_t kPollMs = 100;
static constexpr uint32_t kTaskStack = 4096;
// Above the Matter task (CONFIG_CHIP_TASK_PRIORITY = 1) so the polling keeps its pace, below the Wi-Fi and lwIP
// tasks; the task sleeps between polls.
static constexpr UBaseType_t kTaskPriority = 2;

static presence_report_cb_t s_report;

bool presence_from_target(const ld2410_target_t *t, uint16_t max_cm)
{
    if (max_cm == 0) {
        return t->moving || t->still;
    }
    return (t->moving && t->moving_cm <= max_cm) || (t->still && t->still_cm <= max_cm);
}

// Runs in its own task rather than an esp_timer callback: it logs and may wait for the Matter stack lock (in the
// report callback), which must not stall the esp_timer task.
static void presence_task(void *arg)
{
    bool radar_ok = true;
    bool present = false;
    bool reported = false;
    bool reported_present = false;

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kPollMs));

        ld2410_target_t t = {};
        // Before the first report, count from boot so the radar gets the same grace period.
        const int64_t last_ms = ld2410_get_target(&t) ? t.timestamp_ms : 0;
        const bool fresh = esp_timer_get_time() / 1000 - last_ms <= CONFIG_RADAR_TIMEOUT_SEC * 1000;
        if (fresh != radar_ok) {
            radar_ok = fresh;
            if (fresh) {
                ESP_LOGI(TAG, "Radar data received");
            } else {
                ESP_LOGE(TAG, "No data from the LD2410 for %d s, check the wiring (radar TX -> GPIO%d, RX -> GPIO%d)",
                         CONFIG_RADAR_TIMEOUT_SEC, CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
            }
        }

        // A silent radar counts as "no one", so the room does not stay occupied forever.
        const bool now_present = fresh && presence_from_target(&t, CONFIG_PRESENCE_MAX_DISTANCE_CM);
        if (now_present != present) {
            present = now_present;
            if (fresh) {
                ESP_LOGI(TAG, "Radar: %s (moving %s %u cm, still %s %u cm)", present ? "presence" : "no one",
                         t.moving ? "yes" : "no", t.moving_cm, t.still ? "yes" : "no", t.still_cm);
            } else {
                ESP_LOGI(TAG, "Radar: no one (no radar data)");
            }
        }

        // Only transitions go to the cluster: each "unoccupied" restarts its hold timer. A report that does not
        // reach the cluster is retried on the next poll.
        if ((!reported || present != reported_present) && s_report(present)) {
            reported = true;
            reported_present = present;
        }
    }
}

esp_err_t presence_start(presence_report_cb_t report)
{
    s_report = report;
    if (xTaskCreate(presence_task, "presence", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
