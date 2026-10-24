/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "presence_manager.h"

#include <string.h>

#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "sdkconfig.h"

#include "presence_logic.h"

static const char *TAG = "presence";

static constexpr int kQueueLength = 32;
static constexpr int8_t kRssiUnavailable = 127;
static_assert(PRESENCE_LOGIC_SLOTS == PRESENCE_SLOT_COUNT, "one presence rule per slot");

namespace {

struct SlotMatch {
    bool active;
    uint8_t addr[BLE_ADDR_LEN];
    uint8_t addr_type;
};

struct Packet {
    uint8_t slot;
    int8_t rssi;
};

} // namespace

// Built before the scanner starts, read-only afterwards (also from the host task)
static SlotMatch s_match[PRESENCE_SLOT_COUNT];
static char s_labels[PRESENCE_SLOT_COUNT][PRESENCE_LABEL_MAX_BYTES + 1];
static presence_rule_t s_rules[PRESENCE_SLOT_COUNT];
static presence_report_fn_t s_report;
static QueueHandle_t s_queue;

// NimBLE host task only
static int64_t s_last_post_ms[PRESENCE_SLOT_COUNT];
static bool s_posted[PRESENCE_SLOT_COUNT];
static bool s_pending[PRESENCE_SLOT_COUNT];
static int8_t s_pending_rssi[PRESENCE_SLOT_COUNT];

static std::atomic<uint32_t> s_matched[PRESENCE_SLOT_COUNT];
static std::atomic<uint32_t> s_queue_drops{ 0 };
static std::atomic<uint32_t> s_report_retries{ 0 };

// Snapshot for the diagnostics, written by the presence task
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static presence_manager_stats_t s_stats;

static bool slot_matches(int slot, const uint8_t addr[BLE_ADDR_LEN], uint8_t addr_type)
{
    const SlotMatch &m = s_match[slot];
    return m.active && memcmp(m.addr, addr, BLE_ADDR_LEN) == 0 && beacon_addr_type_matches(m.addr_type, addr_type);
}

bool presence_manager_filter(const uint8_t addr[BLE_ADDR_LEN], uint8_t addr_type)
{
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        if (slot_matches(i, addr, addr_type)) {
            return true;
        }
    }
    return false;
}

static bool stronger(int8_t rssi, int8_t than)
{
    if (rssi == kRssiUnavailable) {
        return false;
    }
    return than == kRssiUnavailable || rssi > than;
}

void presence_manager_sink(const ble_adv_report_t *report)
{
    // Only a well-formed iBeacon frame refreshes a slot; the advertised name is never used to identify a beacon.
    if (!report->has_ibeacon) {
        return;
    }
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        if (!slot_matches(i, report->addr, report->addr_type)) {
            continue;
        }
        s_matched[i].fetch_add(1, std::memory_order_relaxed);
        if (!s_pending[i] || stronger(report->rssi, s_pending_rssi[i])) {
            s_pending_rssi[i] = report->rssi;
        }
        s_pending[i] = true;
        if (s_posted[i] && report->time_ms - s_last_post_ms[i] < CONFIG_PRESENCE_REPORT_THROTTLE_MS) {
            continue;
        }
        const Packet packet = { static_cast<uint8_t>(i), s_pending_rssi[i] };
        if (xQueueSend(s_queue, &packet, 0) == pdTRUE) {
            s_posted[i] = true;
            s_pending[i] = false;
            s_last_post_ms[i] = report->time_ms;
        } else {
            s_queue_drops.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

static void publish_stats(const presence_logic_t &logic, const bool reported[PRESENCE_SLOT_COUNT], uint64_t now)
{
    presence_manager_stats_t st = {};
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        presence_slot_stats_t &s = st.slots[i];
        s.active = s_rules[i].active;
        s.present = logic.state[i].present;
        s.reported = reported[i] == logic.state[i].present;
        s.last_rssi = logic.state[i].last_rssi;
        s.age_s = static_cast<uint32_t>(presence_logic_age_ms(&logic, i, now) / 1000);
        s.matched = s_matched[i].load(std::memory_order_relaxed);
        s.accepted = logic.state[i].accepted;
        s.rssi_rejected = logic.state[i].rssi_rejected;
    }
    st.queue_drops = s_queue_drops.load();
    st.report_retries = s_report_retries.load();
    portENTER_CRITICAL(&s_stats_mux);
    s_stats = st;
    portEXIT_CRITICAL(&s_stats_mux);
}

static void presence_task(void *)
{
    presence_logic_t logic;
    presence_logic_init(&logic, s_rules);
    bool reported[PRESENCE_SLOT_COUNT] = {}; // the Occupancy attribute starts unoccupied at boot
    bool retry_logged[PRESENCE_SLOT_COUNT] = {};

    for (;;) {
        // Apply every queued packet first, then the timeouts: a packet waiting in the queue is never late enough
        // to turn its slot off.
        Packet packet;
        if (xQueueReceive(s_queue, &packet, pdMS_TO_TICKS(250)) == pdTRUE) {
            do {
                const uint64_t now = ble_scanner_scan_clock_ms();
                if (presence_logic_on_packet(&logic, packet.slot, packet.rssi, now)) {
                    ESP_LOGI(TAG, "Presence %d \"%s\": ON (RSSI %d dBm)", packet.slot + 1, s_labels[packet.slot],
                             packet.rssi);
                }
            } while (xQueueReceive(s_queue, &packet, 0) == pdTRUE);
        }

        const uint64_t now = ble_scanner_scan_clock_ms();
        const uint32_t turned_off = presence_logic_tick(&logic, now);
        for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
            if (turned_off & (1u << i)) {
                ESP_LOGI(TAG, "Presence %d \"%s\": OFF (no valid packet for %u s of scanning)", i + 1, s_labels[i],
                         static_cast<unsigned>(s_rules[i].timeout_ms / 1000));
            }
        }

        // Deliver changes; the report function takes the Matter stack lock, this task holds no other lock.
        for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
            const bool present = logic.state[i].present;
            if (present == reported[i]) {
                continue;
            }
            if (s_report(i, present)) {
                reported[i] = present;
                retry_logged[i] = false;
            } else {
                s_report_retries.fetch_add(1);
                if (!retry_logged[i]) {
                    ESP_LOGW(TAG, "Presence %d: Matter not ready, report retried", i + 1);
                    retry_logged[i] = true;
                }
            }
        }
        publish_stats(logic, reported, now);
    }
}

esp_err_t presence_manager_start(const presence_config_t *cfg, presence_report_fn_t report)
{
    s_report = report;
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        const presence_slot_config_t &s = cfg->slots[i];
        s_match[i].active = presence_slot_is_active(&s);
        memcpy(s_match[i].addr, s.addr, BLE_ADDR_LEN);
        s_match[i].addr_type = s.addr_type;
        s_rules[i].active = s_match[i].active;
        s_rules[i].timeout_ms = static_cast<uint32_t>(s.timeout_s) * 1000;
        s_rules[i].rssi_filter = s.rssi_filter;
        s_rules[i].rssi_threshold = s.rssi_threshold;
        s_rules[i].rssi_hysteresis = s.rssi_hysteresis;
        if (s.label[0] != '\0') {
            memcpy(s_labels[i], s.label, sizeof(s_labels[i]));
        } else {
            presence_config_default_label(i, s_labels[i], sizeof(s_labels[i]));
        }

        char addr[BLE_ADDR_STR_SIZE];
        ble_addr_format(s.addr, addr);
        if (s_match[i].active) {
            ESP_LOGI(TAG, "Presence %d \"%s\": %s (%s), absent after %u s%s", i + 1, s_labels[i], addr,
                     beacon_addr_type_name(s.addr_type), s.timeout_s, s.rssi_filter ? ", RSSI filter" : "");
        } else {
            ESP_LOGI(TAG, "Presence %d \"%s\": %s, always off", i + 1, s_labels[i],
                     s.enabled ? "no beacon assigned" : "disabled");
        }
    }
    s_queue = xQueueCreate(kQueueLength, sizeof(Packet));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(presence_task, "presence", 4096, nullptr, 5, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void presence_manager_get_stats(presence_manager_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}
