/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "presence_link.h"

#include <string.h>

#include <atomic>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sdkconfig.h"

#include "presence_config.h"
#if CONFIG_PRESENCE_ROLE_SCANNER
#include "ble_scanner.h"
#endif

static const char *TAG = "link";

static_assert(PLINK_SLOT_COUNT == PRESENCE_SLOT_COUNT, "one link slot per presence slot");

static constexpr uart_port_t kUart = static_cast<uart_port_t>(CONFIG_PRESENCE_LINK_UART_NUM);
static constexpr int64_t kSendPeriodMs = 1000;
static constexpr int64_t kSlowPeriodMs = 5000; // SLOTS (scanner), PAIRING (Matter node)
static constexpr TickType_t kReadTimeout = pdMS_TO_TICKS(100);
#if CONFIG_PRESENCE_ROLE_SCANNER
static constexpr uint32_t kTaskStack = 3584;
#else
// The bridged devices are added and removed in this task (esp-matter endpoints, cluster init, NVS)
static constexpr uint32_t kTaskStack = 6144;
#endif

static bool s_scanner;
static TaskHandle_t s_task;
#if CONFIG_PRESENCE_ROLE_SCANNER
static bool s_config_mode;
static uint8_t s_active_mask;
static uint32_t s_timeout_ms[PRESENCE_SLOT_COUNT];
static portMUX_TYPE s_mask_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_present_mask;  // s_mask_mux
static uint8_t s_reported_mask; // s_mask_mux: slots reported since boot, present and known updated together
static plink_slots_t s_slots;   // s_mask_mux
static bool s_slots_send;       // s_mask_mux
static std::atomic<bool> s_dirty{ false };
static std::atomic<bool> s_slots_dirty{ false };
static std::atomic<uint8_t> s_command{ 0 };
static std::atomic<int> s_command_left{ 0 };
#else
static presence_link_matter_ops_t s_ops;
static bool s_applied[PRESENCE_SLOT_COUNT];
static bool s_applied_valid[PRESENCE_SLOT_COUNT];
static bool s_gone = false; // scanner declared gone: sensors off until it comes back
static plink_slots_t s_want_slots;
static bool s_have_want = false;
static plink_slots_t s_synced_slots;
static bool s_synced = false;
static int64_t s_next_sync_ms = 0; // after a sync that could not complete
static uint8_t s_last_command;
static int64_t s_last_command_ms;
#endif

static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static presence_link_stats_t s_stats;

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

static void send_frame(const uint8_t *frame, size_t len)
{
    if (len == 0) {
        return;
    }
    uart_write_bytes(kUart, frame, len);
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.tx_frames++;
    portEXIT_CRITICAL(&s_stats_mux);
}

#if CONFIG_PRESENCE_ROLE_SCANNER

static void send_state()
{
    ble_scanner_stats_t scan;
    ble_scanner_get_stats(&scan);
    portENTER_CRITICAL(&s_mask_mux);
    const uint8_t present = s_present_mask;
    const uint8_t reported = s_reported_mask;
    portEXIT_CRITICAL(&s_mask_mux);
    const int64_t now = now_ms();
    plink_state_t st = {};
    st.mode = s_config_mode ? PLINK_MODE_CONFIG : PLINK_MODE_NORMAL;
    st.flags = scan.state == BLE_SCANNER_RUNNING ? PLINK_STATE_SCANNING : 0;
    st.active_mask = s_config_mode ? 0 : s_active_mask;
    st.present_mask = s_config_mode ? 0 : present;
    st.known_mask = s_config_mode ? 0
                                  : plink_known_mask(s_active_mask, reported, s_timeout_ms, PRESENCE_SLOT_COUNT,
                                                     scan.scan_clock_ms, static_cast<uint64_t>(now));
    st.uptime_s = static_cast<uint32_t>(now / 1000);
    uint8_t frame[PLINK_MAX_FRAME];
    send_frame(frame, plink_encode_state(&st, frame, sizeof(frame)));
}

static void send_slots()
{
    portENTER_CRITICAL(&s_mask_mux);
    const bool send = s_slots_send;
    const plink_slots_t slots = s_slots;
    portEXIT_CRITICAL(&s_mask_mux);
    if (send) {
        uint8_t frame[PLINK_MAX_FRAME];
        send_frame(frame, plink_encode_slots(&slots, frame, sizeof(frame)));
    }
}

static void on_frame(const plink_parser_t &p)
{
    if (p.type == PLINK_TYPE_STATUS) {
        plink_status_t st;
        if (!plink_decode_status(p.payload, p.len, &st)) {
            return;
        }
        portENTER_CRITICAL(&s_stats_mux);
        const bool first = !s_stats.have_peer_status;
        s_stats.have_peer_status = true;
        s_stats.peer_status = st;
        s_stats.last_rx_ms = now_ms();
        portEXIT_CRITICAL(&s_stats_mux);
        if (first) {
            ESP_LOGI(TAG, "Matter node seen (commissioned %d, Thread up %d, bridged devices 0x%02x)",
                     (st.flags & PLINK_STATUS_COMMISSIONED) != 0, (st.flags & PLINK_STATUS_NETWORK_UP) != 0,
                     st.bridged_mask);
        }
    } else if (p.type == PLINK_TYPE_PAIRING) {
        plink_pairing_t pairing;
        if (plink_decode_pairing(p.payload, p.len, &pairing)) {
            portENTER_CRITICAL(&s_stats_mux);
            s_stats.have_pairing = true;
            s_stats.pairing = pairing;
            portEXIT_CRITICAL(&s_stats_mux);
        }
    }
}

#else // Matter node

static void send_status()
{
    plink_status_t st = {};
    s_ops.status(&st);
    st.uptime_s = static_cast<uint32_t>(now_ms() / 1000);
    uint8_t frame[PLINK_MAX_FRAME];
    send_frame(frame, plink_encode_status(&st, frame, sizeof(frame)));
}

static void send_pairing()
{
    plink_pairing_t pairing = {};
    if (s_ops.pairing(&pairing)) {
        uint8_t frame[PLINK_MAX_FRAME];
        send_frame(frame, plink_encode_pairing(&pairing, frame, sizeof(frame)));
    }
}

// Brings the sensors of known_mask to the wanted states, only the ones that differ (or were never applied). The
// others keep their state: the scanner does not know yet (it just restarted).
static void apply_states(uint8_t want_mask, uint8_t known_mask)
{
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        const bool want = (want_mask >> i) & 1;
        if (((known_mask >> i) & 1) == 0 || (s_applied_valid[i] && s_applied[i] == want)) {
            continue;
        }
        if (s_ops.apply(i, want)) {
            if (!s_applied_valid[i] || s_applied[i] != want) {
                ESP_LOGI(TAG, "Presence %d: %s", i + 1, want ? "ON" : "OFF");
            }
            s_applied[i] = want;
            s_applied_valid[i] = true;
        }
    }
}

// The bridged devices follow the scanner's list; tried again every 5 s until done (Matter not up yet, a device that
// could not be added)
static void sync_bridge(int64_t now)
{
    const bool same = s_synced && memcmp(&s_synced_slots, &s_want_slots, sizeof(s_want_slots)) == 0;
    if (!s_have_want || same || now < s_next_sync_ms) {
        return;
    }
    uint8_t changed = 0;
    const bool done = s_ops.sync(&s_want_slots, &changed);
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        if (changed & (1u << i)) {
            s_applied_valid[i] = false; // a new device starts unoccupied: set its state again
        }
    }
    if (done) {
        s_synced_slots = s_want_slots;
        s_synced = true;
    } else {
        s_synced = false;
        s_next_sync_ms = now + kSlowPeriodMs;
    }
}

static void on_frame(const plink_parser_t &p)
{
    if (p.type == PLINK_TYPE_SLOTS) {
        plink_slots_t slots;
        if (plink_decode_slots(p.payload, p.len, &slots)) {
            s_want_slots = slots;
            s_have_want = true;
        }
        return;
    }
    if (p.type == PLINK_TYPE_COMMAND) {
        plink_command_t cmd;
        if (plink_decode_command(p.payload, p.len, &cmd)) {
            const int64_t now = now_ms();
            // The scanner sends each command three times: act on the first only
            if (cmd.command != s_last_command || now - s_last_command_ms > 5000) {
                ESP_LOGW(TAG, "Command %u from the scanner's configuration page", cmd.command);
                s_ops.command(cmd.command);
            }
            s_last_command = cmd.command;
            s_last_command_ms = now;
        }
        return;
    }
    plink_state_t st;
    if (p.type != PLINK_TYPE_STATE || !plink_decode_state(p.payload, p.len, &st)) {
        return;
    }
    portENTER_CRITICAL(&s_stats_mux);
    const bool first = !s_stats.have_peer_state;
    const uint8_t last_mode = s_stats.peer_state.mode;
    s_stats.have_peer_state = true;
    s_stats.peer_state = st;
    s_stats.last_rx_ms = now_ms();
    portEXIT_CRITICAL(&s_stats_mux);
    if (first || s_gone) {
        ESP_LOGI(TAG, "Scanner %s (mode %s, %u slot(s) assigned, known 0x%02x)", first ? "seen" : "back",
                 st.mode == PLINK_MODE_CONFIG ? "CONFIG" : "NORMAL",
                 static_cast<unsigned>(__builtin_popcount(st.active_mask)), st.known_mask & 0x1F);
        if (s_gone) {
            s_ops.reachable(true);
        }
        s_gone = false;
    }
    if (st.mode == PLINK_MODE_CONFIG) {
        // The scanner computes no presence while its web page runs: keep the states as they are
        if (first || last_mode != PLINK_MODE_CONFIG) {
            ESP_LOGI(TAG, "Scanner in CONFIG mode: sensors keep their states");
        }
        return;
    }
    apply_states(st.present_mask & st.active_mask, st.known_mask);
}

#endif

static void link_task(void *)
{
    plink_parser_t parser;
    plink_parser_init(&parser);
    int64_t last_send = 0;
    int64_t last_slow = -kSlowPeriodMs; // first SLOTS / PAIRING right away
    uint8_t buf[64];
    for (;;) {
        // What is buffered now, or else the first byte to come within kReadTimeout. uart_read_bytes() only returns
        // when it has all the bytes asked for or after kReadTimeout without any byte, so asking for sizeof(buf)
        // would hold the loop for seconds on a trickle of noise.
        size_t buffered = 0;
        uart_get_buffered_data_len(kUart, &buffered);
        const int n = buffered > 0 ? uart_read_bytes(kUart, buf, buffered < sizeof(buf) ? buffered : sizeof(buf), 0)
                                   : uart_read_bytes(kUart, buf, 1, kReadTimeout);
        for (int i = 0; i < n; i++) {
            if (plink_parser_feed(&parser, buf[i])) {
                on_frame(parser);
            }
        }
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.rx_frames = parser.frames;
        s_stats.crc_errors = parser.crc_errors;
        s_stats.bad_frames = parser.bad_frames;
        portEXIT_CRITICAL(&s_stats_mux);

        const int64_t now = now_ms();
#if CONFIG_PRESENCE_ROLE_SCANNER
        if (now - last_send >= kSendPeriodMs || s_dirty.exchange(false)) {
            send_state();
            last_send = now;
        }
        if (now - last_slow >= kSlowPeriodMs || s_slots_dirty.exchange(false)) {
            send_slots();
            last_slow = now;
        }
        // One frame of a requested command per pass (about 100 ms apart)
        int left = s_command_left.load();
        while (left > 0 && !s_command_left.compare_exchange_weak(left, left - 1)) {
        }
        if (left > 0) {
            const plink_command_t cmd = { s_command.load() };
            uint8_t frame[PLINK_MAX_FRAME];
            send_frame(frame, plink_encode_command(&cmd, frame, sizeof(frame)));
        }
#else
        if (now - last_send >= kSendPeriodMs) {
            send_status();
            last_send = now;
        }
        if (now - last_slow >= kSlowPeriodMs) {
            send_pairing();
            last_slow = now;
        }
        sync_bridge(now);
        // Retry the states Matter did not take yet (it starts after the link)
        presence_link_stats_t snap;
        presence_link_get_stats(&snap);
        if (snap.have_peer_state && snap.peer_state.mode == PLINK_MODE_NORMAL && !s_gone) {
            apply_states(snap.peer_state.present_mask & snap.peer_state.active_mask, snap.peer_state.known_mask);
        }
#if CONFIG_PRESENCE_LINK_TIMEOUT_SEC > 0
        const int64_t since = snap.last_rx_ms > 0 ? now - snap.last_rx_ms : now;
        if (!s_gone && since > CONFIG_PRESENCE_LINK_TIMEOUT_SEC * 1000LL) {
            ESP_LOGW(TAG, "No frame from the scanner for %d s: every sensor off and unreachable until it is back",
                     CONFIG_PRESENCE_LINK_TIMEOUT_SEC);
            s_gone = true;
            portENTER_CRITICAL(&s_stats_mux);
            s_stats.link_losses++;
            portEXIT_CRITICAL(&s_stats_mux);
            for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
                s_applied_valid[i] = false; // re-apply below and on every pass until Matter takes it
            }
            s_ops.reachable(false);
        }
        if (s_gone) {
            apply_states(0, 0xFF);
        }
#endif
#endif
    }
}

static esp_err_t start_uart()
{
    uart_config_t cfg = {};
    cfg.baud_rate = CONFIG_PRESENCE_LINK_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(kUart, 512, 0, 0, nullptr, 0);
    if (err == ESP_OK) {
        err = uart_param_config(kUart, &cfg);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(kUart, CONFIG_PRESENCE_LINK_TX_GPIO, CONFIG_PRESENCE_LINK_RX_GPIO, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        // Idle level while the other chip's TX is released (it resets or boots, a loose wire): no floating input
        err = gpio_pullup_en(static_cast<gpio_num_t>(CONFIG_PRESENCE_LINK_RX_GPIO));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d setup failed: %s", CONFIG_PRESENCE_LINK_UART_NUM, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "UART%d, TX GPIO%d, RX GPIO%d, %d baud", CONFIG_PRESENCE_LINK_UART_NUM, CONFIG_PRESENCE_LINK_TX_GPIO,
             CONFIG_PRESENCE_LINK_RX_GPIO, CONFIG_PRESENCE_LINK_BAUD);
    return xTaskCreate(link_task, "link", kTaskStack, nullptr, 5, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

#if CONFIG_PRESENCE_ROLE_SCANNER

esp_err_t presence_link_start_scanner(bool config_mode, const presence_config_t *cfg)
{
    s_scanner = true;
    s_config_mode = config_mode;
    for (int i = 0; cfg != nullptr && i < PRESENCE_SLOT_COUNT; i++) {
        if (presence_slot_is_active(&cfg->slots[i])) {
            s_active_mask |= static_cast<uint8_t>(1u << i);
        }
        s_timeout_ms[i] = static_cast<uint32_t>(cfg->slots[i].timeout_s) * 1000;
    }
    return start_uart();
}

bool presence_link_report(int slot, bool present)
{
    if (slot < 0 || slot >= PRESENCE_SLOT_COUNT) {
        return true;
    }
    const uint8_t bit = static_cast<uint8_t>(1u << slot);
    portENTER_CRITICAL(&s_mask_mux);
    if (present) {
        s_present_mask |= bit;
    } else {
        s_present_mask &= static_cast<uint8_t>(~bit);
    }
    s_reported_mask |= bit;
    portEXIT_CRITICAL(&s_mask_mux);
    s_dirty = true;
    return true;
}

void presence_link_set_slots(const presence_config_t *cfg, bool send)
{
    plink_slots_t slots = {};
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        if (presence_slot_is_active(&cfg->slots[i])) {
            slots.active_mask |= static_cast<uint8_t>(1u << i);
            memcpy(slots.mac[i], cfg->slots[i].addr, PLINK_MAC_LEN);
        }
    }
    portENTER_CRITICAL(&s_mask_mux);
    s_slots = slots;
    s_slots_send = send;
    portEXIT_CRITICAL(&s_mask_mux);
    s_slots_dirty = true;
}

void presence_link_request_command(uint8_t command)
{
    s_command = command;
    s_command_left = 3;
}

#else

esp_err_t presence_link_start_matter(const presence_link_matter_ops_t *ops)
{
    s_scanner = false;
    s_ops = *ops;
    return start_uart();
}

#endif

void presence_link_get_stats(presence_link_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}

void presence_link_log_diagnostics(void)
{
    presence_link_stats_t st;
    presence_link_get_stats(&st);
    const unsigned ago = st.last_rx_ms > 0 ? static_cast<unsigned>((now_ms() - st.last_rx_ms) / 1000) : 0;
    if (s_task != nullptr) {
        ESP_LOGI(TAG, "Link task: %u of %u bytes of stack never used",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(s_task)), static_cast<unsigned>(kTaskStack));
    }
    if (s_scanner) {
        if (!st.have_peer_status) {
            ESP_LOGW(TAG, "Matter node never heard: tx %u, rx %u, CRC errors %u, bad frames %u (check the wiring; "
                          "bad frames: flash both chips with the same firmware version)",
                     static_cast<unsigned>(st.tx_frames), static_cast<unsigned>(st.rx_frames),
                     static_cast<unsigned>(st.crc_errors), static_cast<unsigned>(st.bad_frames));
            return;
        }
        ESP_LOGI(TAG,
                 "Matter node %u s ago: commissioned %d, Thread up %d, %u fabric(s), bridged devices 0x%02x, up %u s; "
                 "tx %u, rx %u, CRC errors %u, bad frames %u",
                 ago, (st.peer_status.flags & PLINK_STATUS_COMMISSIONED) != 0,
                 (st.peer_status.flags & PLINK_STATUS_NETWORK_UP) != 0, st.peer_status.fabrics,
                 st.peer_status.bridged_mask, static_cast<unsigned>(st.peer_status.uptime_s),
                 static_cast<unsigned>(st.tx_frames), static_cast<unsigned>(st.rx_frames),
                 static_cast<unsigned>(st.crc_errors), static_cast<unsigned>(st.bad_frames));
        return;
    }
    if (!st.have_peer_state) {
        ESP_LOGW(TAG, "Scanner never heard: tx %u, rx %u, CRC errors %u, bad frames %u (check the wiring; bad frames: "
                      "flash both chips with the same firmware version)",
                 static_cast<unsigned>(st.tx_frames), static_cast<unsigned>(st.rx_frames),
                 static_cast<unsigned>(st.crc_errors), static_cast<unsigned>(st.bad_frames));
        return;
    }
    ESP_LOGI(TAG,
             "Scanner %u s ago: %s, scanning %d, assigned 0x%02x, present 0x%02x, known 0x%02x, up %u s; tx %u, rx %u, "
             "CRC errors %u, bad frames %u, losses %u",
             ago, st.peer_state.mode == PLINK_MODE_CONFIG ? "CONFIG" : "NORMAL",
             (st.peer_state.flags & PLINK_STATE_SCANNING) != 0, st.peer_state.active_mask, st.peer_state.present_mask,
             st.peer_state.known_mask & 0x1F, static_cast<unsigned>(st.peer_state.uptime_s),
             static_cast<unsigned>(st.tx_frames), static_cast<unsigned>(st.rx_frames),
             static_cast<unsigned>(st.crc_errors), static_cast<unsigned>(st.bad_frames),
             static_cast<unsigned>(st.link_losses));
}
