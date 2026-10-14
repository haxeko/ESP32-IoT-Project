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
    for (size_t i = 1; data[0] == 0x00 && i + 1 < len; i++) {
        if (data[i] == 0x8D && data[i + 1] == 0x14) {
            g_sim.panel_configured = true;
        }
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
