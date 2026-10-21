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

#if CONFIG_PRESENCE_ENERGY_FILTER
#include "energy_filter.h"
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
#include "esp_presence.h"
#endif

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

#if CONFIG_PRESENCE_ENERGY_FILTER
// Logs what the energy filter did since the last poll (one event per poll at most).
static void log_filter_event()
{
    energy_filter_state_t ev;
    if (!energy_filter_take_event(&ev)) {
        return;
    }
    const energy_filter_peak_t &p = ev.peak;
    const char *kind = p.moving ? "moving" : "still";
    switch (ev.event) {
    case ENERGY_FILTER_STRONG:
        ESP_LOGI(TAG, "Energy filter: %s gate %u energy %u, threshold %u (+%d): occupied at once", kind, p.gate,
                 p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_CONFIRMED:
        ESP_LOGI(TAG, "Energy filter: %u reports in a row above a threshold (highest %s gate %u energy %u, "
                      "threshold %u, +%d): occupied",
                 ev.event_frames, kind, p.gate, p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_IGNORED:
        ESP_LOGI(TAG, "Energy filter: ignored %u report%s above a threshold (highest %s gate %u energy %u, "
                      "threshold %u, +%d)",
                 ev.event_frames, ev.event_frames == 1 ? "" : "s", kind, p.gate, p.energy, p.threshold, p.excess);
        break;
    case ENERGY_FILTER_NO_EVENT:
        break;
    }
}
#endif

#if CONFIG_PRESENCE_ESP_ENGINE
// The ESP32 presence engine decides (esp_presence.cpp, which logs why); this task hands its occupancy to the
// cluster and its HoldTime to the engine (the absence countdown of both). A silent radar is a sensor fault for the
// engine, not "no one": the occupancy then stays as it is.
static constexpr uint32_t kHoldTimeReadPolls = 10; // HoldTime read every second

static presence_hold_time_cb_t s_hold_time;

void presence_set_hold_time_source(presence_hold_time_cb_t read_hold_time)
{
    s_hold_time = read_hold_time;
}

static void presence_task(void *arg)
{
    bool reported = false;
    bool reported_present = false;
    uint32_t polls = 0;
    ESP_LOGI(TAG, "Occupancy from the ESP32 presence engine");
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kPollMs));
        uint16_t hold_s = 0;
        if (polls++ % kHoldTimeReadPolls == 0 && s_hold_time && s_hold_time(&hold_s)) {
            esp_presence_set_hold_time(hold_s);
        }
        // Occupied, or present but counting down (false: the cluster counts HoldTime from then on, as the engine)
        const bool present = esp_presence_occupied();
        if ((!reported || present != reported_present) && s_report(present)) {
            reported = true;
            reported_present = present;
        }
    }
}
#else
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
#if CONFIG_PRESENCE_ENERGY_FILTER
        log_filter_event();
        bool filtered = false;
        const bool target = energy_filter_presence(&filtered) ? filtered
                                                               : presence_from_target(&t, CONFIG_PRESENCE_MAX_DISTANCE_CM);
#else
        const bool target = presence_from_target(&t, CONFIG_PRESENCE_MAX_DISTANCE_CM);
#endif
        const bool now_present = fresh && target;
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
#endif

esp_err_t presence_start(presence_report_cb_t report)
{
    s_report = report;
#if CONFIG_PRESENCE_ENERGY_FILTER
    energy_filter_start();
#endif
    if (xTaskCreate(presence_task, "presence", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
