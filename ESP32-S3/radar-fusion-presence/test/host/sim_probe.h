// Simulated clock, UARTs and radars for the host test of the radar probe
// (radar_probe/main/probe_radar.cpp). The ESP-IDF stubs in stubs/ call into it.
// Time only moves when the probe waits (UART event queue, vTaskDelay); SimEnd
// is thrown when it reaches g_sim.end_us, which ends the channel task.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

struct SimEnd {};

struct SimBurst {
    int64_t end_us; // last byte on the wire
    std::vector<uint8_t> bytes;
};

// A radar as its protocol documents describe it: reports every 100 ms outside
// the configuration mode, an ACK about 3 ms after each command it understands.
struct SimRadar {
    enum Kind { NONE, LD2410, LD2450 } kind = NONE;
    uint32_t baud = 256000;
    bool config = false;
    bool engineering = false;
    bool old_firmware = false;    // LD2410 without the auxiliary control and noise detection commands
    uint32_t next_baud = 0;       // stored by command 0x00A1, used after a restart
    uint32_t restart_baud = 0;    // rate the radar comes back with from the restart under way
    int64_t silent_until_us = 0;  // restarting
    int64_t next_report_us = 40000;
    std::deque<SimBurst> acks;
    std::vector<uint16_t> commands; // command words understood, in order
};

struct SimPort {
    SimRadar radar;
    uint32_t baud = 0; // ESP32 side
    std::vector<uint8_t> rx;
};

struct SimTask {
    void (*fn)(void *);
    void *arg;
};

struct Sim {
    int64_t now_us = 0;
    int64_t end_us = 0;
    SimPort port[3];
    std::vector<SimTask> tasks;
    std::vector<std::string> records;                           // probe output lines, without '\n'
    std::vector<std::pair<int64_t, std::function<void()>>> at;  // actions at a time
    std::vector<int64_t> report_ends;                           // end of every report sent (any port)
    uint32_t noise = 1;
};

extern Sim g_sim;
extern int g_failures;

void sim_reset(int64_t end_ms);
void sim_advance_us(int64_t us);
void sim_record(const char *data, size_t len);
// Records whose text contains needle
int sim_count(const char *needle);
const std::string *sim_find(const char *needle);

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            g_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)
