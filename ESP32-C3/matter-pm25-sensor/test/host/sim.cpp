#include "sim.h"

#include <stdarg.h>
#include <string.h>

Sim g_sim;
int g_failures;

void sim_reset(int64_t end_ms)
{
    g_sim = Sim();
    g_sim.end_ms = end_ms;
}

int64_t sim_ms()
{
    return g_sim.now_us / 1000;
}

void sim_advance(int ms)
{
    g_sim.now_us += (int64_t)ms * 1000;
    if (g_sim.on_tick) {
        g_sim.on_tick();
    }
    if (sim_ms() >= g_sim.end_ms) {
        throw SimEnd();
    }
}

int sim_uart_read(void *buf, uint32_t len, int timeout_ms)
{
    if (sim_ms() >= g_sim.end_ms) {
        throw SimEnd();
    }
    if (g_sim.uart_next < g_sim.uart_rx.size() && g_sim.uart_rx[g_sim.uart_next].t_ms <= sim_ms() + timeout_ms) {
        SimChunk &chunk = g_sim.uart_rx[g_sim.uart_next];
        if (chunk.t_ms > sim_ms()) {
            g_sim.now_us = chunk.t_ms * 1000;
        }
        const size_t n = chunk.bytes.size() < len ? chunk.bytes.size() : len;
        memcpy(buf, chunk.bytes.data(), n);
        chunk.bytes.erase(chunk.bytes.begin(), chunk.bytes.begin() + n);
        if (chunk.bytes.empty()) {
            g_sim.uart_next++;
        }
        return (int)n;
    }
    sim_advance(timeout_ms);
    return 0;
}

int sim_uart_write(const void *buf, size_t len)
{
    std::string s = std::to_string(sim_ms());
    for (size_t i = 0; i < len; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), " %02X", ((const uint8_t *)buf)[i]);
        s += hex;
    }
    g_sim.uart_tx.push_back(s);
    return (int)len;
}

int sim_oled_write(const uint8_t *data, size_t len)
{
    if (!g_sim.oled_present) {
        return -1;
    }
    if (len == 2 && data[0] == 0x00 && (data[1] == 0xAF || data[1] == 0xAE)) {
        g_sim.panel_on = data[1] == 0xAF;
    }
    if (len == 4 && data[0] == 0x00 && (data[1] & 0xF0) == 0xB0) {     // page, column low, column high
        g_sim.oled_page = data[1] & 0x07;
        g_sim.oled_column = (data[2] & 0x0F) | ((data[3] & 0x0F) << 4);
    }
    if (len == 129 && data[0] == 0x40) {    // one page of pixels
        memcpy(g_sim.oled_frame[g_sim.oled_page], &data[1], 128);
    }
    for (size_t i = 1; data[0] == 0x00 && i + 1 < len; i++) {
        if (data[i] == 0x8D && data[i + 1] == 0x14) {
            g_sim.panel_configured = true;
        }
    }
    return 0;
}

uint8_t sim_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            const bool top = ((crc >> 7) ^ (data[i] >> bit)) & 1;
            crc = (uint8_t)(crc << 1) ^ (top ? 0x31 : 0);
        }
    }
    return crc;
}

// Behaves as described in the SCD4x datasheet: no answer for 500 ms after a
// stop, serial number only while idle, read_measurement only with a new one.
int sim_scd4x_write(const uint8_t *data, size_t len)
{
    if (len != 2) {
        g_sim.scd4x_violations++;
        return -1;
    }
    const uint16_t cmd = (uint16_t)((data[0] << 8) | data[1]);
    g_sim.scd4x_commands.push_back(cmd);    // also those that get no answer
    if (!g_sim.scd4x_present) {
        g_sim.scd4x_unanswered++;
        return -1;
    }
    if (sim_ms() - g_sim.scd4x_stop_ms < 500) {
        g_sim.scd4x_violations++;
        return -1;
    }
    switch (cmd) {
    case 0x3F86:    // stop_periodic_measurement
        g_sim.scd4x_measuring = false;
        g_sim.scd4x_stop_ms = sim_ms();
        break;
    case 0x21B1:    // start_periodic_measurement
        if (g_sim.scd4x_measuring) {
            return -1;
        }
        g_sim.scd4x_measuring = true;
        g_sim.scd4x_start_ms = sim_ms();
        g_sim.scd4x_read = 0;
        break;
    case 0x3682:    // get_serial_number
        if (g_sim.scd4x_measuring) {
            return -1;
        }
        break;
    case 0xE4B8:    // get_data_ready_status
    case 0xEC05:    // read_measurement
        break;
    default:
        return -1;
    }
    g_sim.scd4x_command = cmd;
    g_sim.scd4x_command_ms = sim_ms();
    return 0;
}

int sim_scd4x_read(uint8_t *data, size_t len)
{
    if (!g_sim.scd4x_present) {
        return -1;
    }
    const uint16_t cmd = g_sim.scd4x_command;
    g_sim.scd4x_command = 0;    // a response is read once
    if (cmd == 0 || sim_ms() - g_sim.scd4x_command_ms < 1) {
        g_sim.scd4x_violations++;
        return -1;
    }
    const int ready = g_sim.scd4x_measuring ? (int)((sim_ms() - g_sim.scd4x_start_ms) / 5000) : 0;
    std::vector<uint16_t> words;
    switch (cmd) {
    case 0x3682:
        words = { 0x1234, 0x5678, 0x9ABC };
        break;
    case 0xE4B8:
        words = { (uint16_t)(ready > g_sim.scd4x_read ? 0x8006 : 0x8000) };
        break;
    case 0xEC05:
        if (ready <= g_sim.scd4x_read) {
            return -1;      // no new measurement: NACK
        }
        g_sim.scd4x_read = ready;
        words = { g_sim.scd4x_co2, 0x6666, 0x8000 };   // 25 C, 50 %RH
        break;
    default:
        g_sim.scd4x_violations++;   // command without a response
        return -1;
    }
    if (len != words.size() * 3) {
        g_sim.scd4x_violations++;
        return -1;
    }
    for (size_t i = 0; i < words.size(); i++) {
        data[3 * i] = words[i] >> 8;
        data[3 * i + 1] = words[i] & 0xFF;
        data[3 * i + 2] = sim_crc8(&data[3 * i], 2);
    }
    if (cmd == 0xEC05 && g_sim.scd4x_corrupt > 0) {
        g_sim.scd4x_corrupt--;
        data[0] ^= 0x10;    // CO2 + 4096 ppm, with the CRC of the right value
    }
    return 0;
}

void sim_log(char level, const char *tag, const char *fmt, ...)
{
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    g_sim.log.push_back(std::to_string(sim_ms()) + " " + level + " " + tag + ": " + msg);
}

bool sim_log_contains(const char *text)
{
    for (const std::string &line : g_sim.log) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}
