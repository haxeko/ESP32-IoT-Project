#include "sim_probe.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/queue.h"
#include "hlk_frame.h"

Sim g_sim;
int g_failures;

struct SimQueue {
    uint32_t length;
    uint32_t item_size;
    std::deque<std::vector<uint8_t>> items;
    int uart_port; // >= 0: the UART event queue of that port
};

static hlk_parser_t s_command_parser[3];
// Queues of the current test; a std::deque keeps their addresses while it grows
static std::deque<SimQueue> s_queues;

void sim_reset(int64_t end_ms)
{
    g_sim = Sim();
    g_sim.end_us = end_ms * 1000;
    s_queues.clear();
    for (hlk_parser_t &p : s_command_parser) {
        hlk_parser_init(&p);
    }
}

void sim_advance_us(int64_t us)
{
    g_sim.now_us += us;
    for (size_t i = 0; i < g_sim.at.size(); i++) {
        if (g_sim.at[i].first <= g_sim.now_us) {
            auto action = g_sim.at[i].second;
            g_sim.at.erase(g_sim.at.begin() + (long)i);
            i--;
            action();
        }
    }
    if (g_sim.now_us >= g_sim.end_us) {
        throw SimEnd();
    }
}

void sim_record(const char *data, size_t len)
{
    while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\0')) {
        len--;
    }
    g_sim.records.emplace_back(data, len);
}

int sim_count(const char *needle)
{
    int n = 0;
    for (const std::string &r : g_sim.records) {
        n += r.find(needle) != std::string::npos;
    }
    return n;
}

const std::string *sim_find(const char *needle)
{
    for (const std::string &r : g_sim.records) {
        if (r.find(needle) != std::string::npos) {
            return &r;
        }
    }
    return nullptr;
}

// ---- radar ----

static int64_t byte_us(uint32_t baud)
{
    return 10 * 1000000LL / baud;
}

static std::vector<uint8_t> frame(const uint8_t *header, const std::vector<uint8_t> &payload, const uint8_t *footer)
{
    std::vector<uint8_t> f(header, header + 4);
    f.push_back((uint8_t)payload.size());
    f.push_back((uint8_t)(payload.size() >> 8));
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), footer, footer + 4);
    return f;
}

static std::vector<uint8_t> report(const SimRadar &r)
{
    static const uint8_t k_header[] = { 0xF4, 0xF3, 0xF2, 0xF1 };
    static const uint8_t k_footer[] = { 0xF8, 0xF7, 0xF6, 0xF5 };
    if (r.kind == SimRadar::LD2450) {
        // The example of the LD2450 protocol document: one target at (-782, 1713) mm
        std::vector<uint8_t> f = { 0xAA, 0xFF, 0x03, 0x00, 0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01 };
        f.insert(f.end(), 16, 0x00);
        f.push_back(0x55);
        f.push_back(0xCC);
        return f;
    }
    // Still target at 150 cm, energy 40
    std::vector<uint8_t> p = { (uint8_t)(r.engineering ? 0x01 : 0x02), 0xAA, 0x02, 0x00, 0x00, 0x00, 0x96, 0x00, 0x28,
                               0x96, 0x00 };
    if (r.engineering) {
        p.push_back(8);
        p.push_back(8);
        for (int g = 0; g < 9; g++) {
            p.push_back((uint8_t)(10 + g));
        }
        for (int g = 0; g < 9; g++) {
            p.push_back((uint8_t)(20 + g));
        }
        p.push_back(0x60); // light
        p.push_back(0x01); // OUT pin
    }
    p.push_back(0x55);
    p.push_back(0x00);
    return frame(k_header, p, k_footer);
}

static void respond(SimPort &port, uint16_t command, const uint8_t *value, size_t value_len)
{
    SimRadar &r = port.radar;
    if (command != HLK_CMD_ENABLE_CONFIG && !r.config) {
        return; // only accepted in configuration mode
    }
    uint16_t status = 0;
    std::vector<uint8_t> v;
    const bool ld2410 = r.kind == SimRadar::LD2410;
    bool restart = false;
    switch (command) {
    case HLK_CMD_ENABLE_CONFIG:
        r.config = true;
        v = { 0x01, 0x00, 0x40, 0x00 };
        break;
    case HLK_CMD_END_CONFIG:
        r.config = false;
        r.next_report_us = g_sim.now_us + 50000;
        break;
    case HLK_CMD_READ_VERSION:
        v = ld2410 ? std::vector<uint8_t>{ 0x01, 0x00, 0x44, 0x02, 0x10, 0x31, 0x07, 0x24 }  // V2.44.24073110
                   : std::vector<uint8_t>{ 0x00, 0x00, 0x04, 0x02, 0x15, 0x19, 0x10, 0x23 }; // V2.04.23101915
        break;
    case HLK_CMD_READ_MAC:
        v = ld2410 ? std::vector<uint8_t>{ 0x08, 0x05, 0x04, 0x03, 0x02, 0x01 }
                   : std::vector<uint8_t>{ 0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65 };
        break;
    case HLK_CMD_SET_BAUD:
        r.next_baud = hlk_baud_rate(value_len >= 2 ? hlk_u16(value) : 0);
        break;
    case HLK_CMD_BLUETOOTH:
        break;
    case HLK_CMD_RESTART:
        restart = true;
        break;
    case 0x0061: // LD2410 read parameters
        if (ld2410) {
            v = { 0xAA, 0x08, 0x08, 0x08, 50, 50, 40, 30, 20, 15, 15, 15, 15, 0, 0, 40, 40, 30, 30, 20, 20, 20, 0x05, 0x00 };
        } else {
            status = 1;
        }
        break;
    case 0x00AB: // LD2410 read resolution
        if (ld2410) {
            v = { 0x00, 0x00 };
        } else {
            status = 1;
        }
        break;
    case 0x00AE: // LD2410B/C auxiliary control
        if (ld2410 && !r.old_firmware) {
            v = { 0x00, 0x80, 0x00, 0x00 };
        } else {
            status = 1;
        }
        break;
    case 0x001B: // LD2410B/C noise detection status
        if (ld2410 && !r.old_firmware) {
            v = { 0x00, 0x00 };
        } else {
            status = 1;
        }
        break;
    case 0x0062:
    case 0x0063:
        if (ld2410) {
            r.engineering = command == 0x0062;
        } else {
            status = 1;
        }
        break;
    case 0x0091: // LD2450 tracking mode
        if (!ld2410) {
            v = { 0x02, 0x00 };
        } else {
            status = 1;
        }
        break;
    case 0x00C1: // LD2450 zones
        if (!ld2410) {
            v = std::vector<uint8_t>(26, 0);
        } else {
            status = 1;
        }
        break;
    default:
        status = 1;
        break;
    }
    if (status == 0) {
        r.commands.push_back(command);
    }
    static const uint8_t k_header[] = { 0xFD, 0xFC, 0xFB, 0xFA };
    static const uint8_t k_footer[] = { 0x04, 0x03, 0x02, 0x01 };
    std::vector<uint8_t> p = { (uint8_t)(command & 0xFF), (uint8_t)((command >> 8) | 0x01), (uint8_t)status, 0x00 };
    p.insert(p.end(), v.begin(), v.end());
    const std::vector<uint8_t> ack = frame(k_header, p, k_footer);
    const int64_t end = g_sim.now_us + 3000 + (int64_t)ack.size() * byte_us(r.baud);
    r.acks.push_back({ end, ack });
    if (restart) {
        r.restart_baud = r.next_baud;
        r.next_baud = 0;
        r.config = false;
        r.engineering = false; // lost at each restart
        r.silent_until_us = end + 1000000;
        r.next_report_us = r.silent_until_us;
    }
}

// Next burst the ESP32 receives before deadline, delivered as UART_DATA
static bool next_burst(int port_num, int64_t deadline, size_t *size)
{
    SimPort &port = g_sim.port[port_num];
    SimRadar &r = port.radar;
    int64_t t_ack = r.acks.empty() ? INT64_MAX : r.acks.front().end_us;
    int64_t t_report = INT64_MAX;
    if (r.kind != SimRadar::NONE && !r.config) {
        t_report = r.next_report_us > r.silent_until_us ? r.next_report_us : r.silent_until_us;
    }
    const int64_t t = t_ack < t_report ? t_ack : t_report;
    // UART_DATA comes 3 symbol times after the burst
    const int64_t deliver = t == INT64_MAX ? INT64_MAX : t + 3 * byte_us(r.baud);
    if (deliver > deadline) {
        return false;
    }
    // Time first: SimEnd at the end of the test must not leave a burst half sent
    sim_advance_us(deliver - g_sim.now_us);
    std::vector<uint8_t> bytes;
    if (t == t_ack) {
        bytes = r.acks.front().bytes;
        r.acks.pop_front();
    } else {
        bytes = report(r);
        r.next_report_us = t + 100000;
        g_sim.report_ends.push_back(t);
    }
    if (r.restart_baud && t >= r.silent_until_us) {
        r.baud = r.restart_baud; // the stored rate, from the restart on
        r.restart_baud = 0;
    }
    if (port.baud != r.baud) {
        for (uint8_t &b : bytes) { // what a UART at the wrong rate makes of it
            g_sim.noise = g_sim.noise * 1103515245 + 12345;
            b = (uint8_t)(g_sim.noise >> 16);
        }
    }
    port.rx.insert(port.rx.end(), bytes.begin(), bytes.end());
    *size = bytes.size();
    return true;
}

// ---- ESP-IDF stubs ----

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size)
{
    s_queues.push_back(SimQueue{ length, item_size, {}, -1 });
    return &s_queues.back();
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t)
{
    if (q->items.size() >= q->length) {
        return pdFALSE;
    }
    const uint8_t *p = (const uint8_t *)item;
    q->items.emplace_back(p, p + q->item_size);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    if (q->uart_port >= 0) {
        const int64_t deadline = ticks == portMAX_DELAY ? INT64_MAX / 2 : g_sim.now_us + (int64_t)ticks * 1000;
        size_t size = 0;
        if (next_burst(q->uart_port, deadline, &size)) {
            const uart_event_t ev = { UART_DATA, size, true };
            memcpy(item, &ev, sizeof(ev));
            return pdTRUE;
        }
        sim_advance_us(deadline - g_sim.now_us);
        return pdFALSE;
    }
    if (!q->items.empty()) {
        memcpy(item, q->items.front().data(), q->item_size);
        q->items.pop_front();
        return pdTRUE;
    }
    if (ticks) {
        sim_advance_us((int64_t)ticks * 1000);
    }
    return pdFALSE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    q->items.clear();
    return pdTRUE;
}

esp_err_t uart_driver_install(uart_port_t port, int, int, int queue_len, QueueHandle_t *queue, int)
{
    s_queues.push_back(SimQueue{ (uint32_t)queue_len, sizeof(uart_event_t), {}, port });
    *queue = &s_queues.back();
    return ESP_OK;
}

int uart_read_bytes(uart_port_t port, void *buf, uint32_t len, TickType_t)
{
    std::vector<uint8_t> &rx = g_sim.port[port].rx;
    const size_t n = rx.size() < len ? rx.size() : len;
    memcpy(buf, rx.data(), n);
    rx.erase(rx.begin(), rx.begin() + (long)n);
    return (int)n;
}

static void on_command(const hlk_frame_t *f, void *ctx)
{
    if (f->kind == HLK_FRAME_COMMAND) {
        respond(*(SimPort *)ctx, hlk_u16(f->payload), f->payload + 2, f->payload_len - 2);
    }
}

int uart_write_bytes(uart_port_t port, const void *data, size_t len)
{
    SimPort &p = g_sim.port[port];
    const SimRadar &r = p.radar;
    if (r.kind != SimRadar::NONE && p.baud == r.baud && g_sim.now_us >= r.silent_until_us) {
        hlk_parser_feed(&s_command_parser[port], (const uint8_t *)data, len, on_command, &p);
    }
    return (int)len;
}
