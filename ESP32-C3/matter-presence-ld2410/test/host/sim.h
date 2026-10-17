// Simulated time, tasks, queues, GPIO, esp_timer, NVS and logs used by the host tests: the ESP-IDF and FreeRTOS
// stubs in stubs/ call into it. One task runs at a time (the test's own thread): time only moves when the code
// under test waits (vTaskDelay, ulTaskNotifyTake, xQueueReceive...), one millisecond at a time, and each simulated
// millisecond runs the hooks (radar simulator, scheduled events, esp_timer callbacks). SimEnd is thrown when the
// time reaches g_sim.end_ms, which ends the endless task loop under test.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct SimEnd {};

struct SimQueue {
    size_t length;
    size_t item_size;
    std::deque<std::vector<uint8_t>> items;
};

struct SimTimer {
    void (*cb)(void *);
    void *arg;
    int64_t period_us; // 0 while not started
    int64_t next_us;
};

struct SimEvent {
    int64_t at_ms;
    std::function<void()> fn;
};

struct Sim {
    int64_t now_us = 0;
    int64_t end_ms = INT64_MAX;
    uint32_t notify = 0;                                         // notifications of the task under test
    std::vector<std::function<void()>> hooks;                    // run after each simulated millisecond
    std::vector<SimEvent> events;                                // run once at their time (sim_at)
    std::function<void(const uint8_t *, size_t)> on_uart_write;  // bytes the ESP32 sends to the radar
    std::vector<SimTimer> timers;                                // esp_timer
    bool run_timers = false;                                     // call the periodic esp_timers as time goes by
    std::map<int, int> gpio_out;                                 // last level written per pin
    std::map<int, int> gpio_in;                                  // level read per pin (1 when not set)
    std::vector<std::string> tasks;                              // names passed to xTaskCreate
    std::map<std::string, std::map<std::string, std::vector<uint8_t>>> nvs; // namespace -> key -> blob
    std::vector<std::string> log;                                // "<ms> <level> <tag>: <message>"
};

extern Sim g_sim;
extern int g_failures;
extern bool g_verbose; // print the log lines as they come (run a test binary with -v)

// Clears everything but the NVS contents (kept like the flash of a board) unless clear_nvs.
void sim_reset(bool clear_nvs = true);
int64_t sim_ms();
// Advances the simulated time by ms milliseconds, running the hooks each millisecond.
void sim_advance(uint32_t ms);
// Runs fn once when the simulated time reaches at_ms.
void sim_at(int64_t at_ms, std::function<void()> fn);
void sim_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
bool sim_log_contains(const char *text);
int sim_log_count(const char *text);
void sim_print_log();

// Test names and checks
void test_begin(const char *name);
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            g_failures++;                                                                                              \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
        }                                                                                                              \
    } while (0)
int test_end(); // prints the result, returns the exit code
