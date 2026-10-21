#include "sim.h"

#include <stdarg.h>
#include <string.h>

Sim g_sim;
int g_failures;
bool g_verbose;

void sim_reset(bool clear_nvs)
{
    auto nvs = g_sim.nvs;
    g_sim = Sim();
    if (!clear_nvs) {
        g_sim.nvs = nvs;
    }
}

int64_t sim_ms()
{
    return g_sim.now_us / 1000;
}

static void run_ms()
{
    for (size_t i = 0; i < g_sim.events.size();) {
        if (g_sim.events[i].at_ms <= sim_ms()) {
            const std::function<void()> fn = g_sim.events[i].fn;
            g_sim.events.erase(g_sim.events.begin() + i);
            fn();
        } else {
            i++;
        }
    }
    for (size_t i = 0; i < g_sim.hooks.size(); i++) {
        g_sim.hooks[i]();
    }
    if (g_sim.run_timers) {
        for (SimTimer &t : g_sim.timers) {
            if (t.period_us > 0 && g_sim.now_us >= t.next_us) {
                t.next_us += t.period_us;
                t.cb(t.arg);
            }
        }
    }
}

void sim_advance(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        if (sim_ms() >= g_sim.end_ms) {
            throw SimEnd();
        }
        g_sim.now_us += 1000;
        run_ms();
    }
    if (sim_ms() >= g_sim.end_ms) {
        throw SimEnd();
    }
}

void sim_at(int64_t at_ms, std::function<void()> fn)
{
    g_sim.events.push_back({ at_ms, fn });
}

void sim_log(char level, const char *tag, const char *fmt, ...)
{
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    const std::string line = std::to_string(sim_ms()) + " " + level + " " + tag + ": " + msg;
    g_sim.log.push_back(line);
    if (g_verbose) {
        printf("      %s\n", line.c_str());
    }
}

bool sim_log_contains(const char *text)
{
    return sim_log_count(text) > 0;
}

int sim_log_count(const char *text)
{
    int n = 0;
    for (const std::string &line : g_sim.log) {
        if (line.find(text) != std::string::npos) {
            n++;
        }
    }
    return n;
}

void test_begin(const char *name)
{
    printf("  %s\n", name);
}

int test_end()
{
    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
