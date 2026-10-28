// Simulated clock, UART, OLED (I2C), GPIO, timers and NVS used by the host
// tests: the ESP-IDF stubs in stubs/ call into it. Time only moves when the code under
// test waits (vTaskDelay, ulTaskNotifyTake, uart_read_bytes), and SimEnd is
// thrown when it reaches g_sim.end_ms, which ends the task loop under test.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct SimEnd {};

struct SimTimer {
    void (*cb)(void *);
    void *arg;
    int64_t period_us;
};

struct SimChunk {
    int64_t t_ms;                   // when the bytes arrive
    std::vector<uint8_t> bytes;
};

struct Sim {
    int64_t now_us = 0;
    int64_t end_ms = 0;
    std::function<void()> on_tick;              // called each time the code under test waits
    std::vector<SimChunk> uart_rx;              // bytes the UART receives
    size_t uart_next = 0;
    std::vector<std::string> uart_tx;           // "<ms> <hex bytes>" of each write
    bool oled_present = true;                   // false: I2C writes fail (unplugged)
    bool panel_on = false;                      // last 0xAF/0xAE command
    bool panel_configured = false;              // settings (charge pump 0x8D 0x14) received
    int oled_column = -1;                       // column address of the last page write
    std::vector<int> gpio_level = std::vector<int>(22, 1);     // per GPIO, 0 = button pressed
    std::vector<SimTimer> timers;               // esp_timers created, in order
    std::map<std::string, uint8_t> nvs;         // "<namespace>/<key>" -> u8 value
    int nvs_writes = 0;
    int wdt_resets = 0;
    std::vector<std::string> log;               // "<ms> <level> <tag>: <message>"
};

extern Sim g_sim;
extern int g_failures;

void sim_reset(int64_t end_ms);
int64_t sim_ms();
void sim_advance(int ms);
int sim_uart_read(void *buf, uint32_t len, int timeout_ms);
int sim_uart_write(const void *buf, size_t len);
int sim_oled_write(const uint8_t *data, size_t len);
void sim_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
bool sim_log_contains(const char *text);

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            g_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)
