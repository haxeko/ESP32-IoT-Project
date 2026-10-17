// Simulated HLK-LD2410: answers the configuration commands the ESP32 writes to the UART (sim.h on_uart_write) and
// sends target reports every 100 ms outside of the configuration mode, both through ld2410_feed(). Settings live in
// the radar ("flash"); the resolution and Bluetooth stored by a command are used after a restart, like the real
// radar. Faults and missing features of older radars are switches below.
#pragma once

#include <stdint.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

struct RadarSim {
    // Features (true: LD2410C with firmware V2.44.25070917)
    bool has_resolution = true; // 0x00AA / 0x00AB
    bool has_bluetooth = true;  // 0x00A4 / 0x00A5
    bool has_auto = true;       // 0x000B / 0x001B
    // Unsupported commands are refused (status 1) when true, not answered when false
    bool refuse_unsupported = true;

    // Stored settings
    uint8_t moving[9] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
    uint8_t still[9] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };
    uint8_t max_moving_gate = 8;
    uint8_t max_still_gate = 8;
    uint16_t no_one_duration = 5;
    uint16_t resolution = 0; // in use: 0 = 0.75 m, 1 = 0.2 m
    uint16_t stored_resolution = 0;
    bool bluetooth = true; // in use (the radar leaves the factory with Bluetooth on)
    bool stored_bluetooth = true;
    uint8_t mac[6] = { 0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65 };

    // State
    bool config_mode = false;
    bool engineering = false;
    bool offline = false; // restarting
    int restarts = 0;
    std::vector<uint16_t> commands; // every command word received
    // Target reported (states 0-3) and the engineering energies of each gate (moving, still) at a time
    uint8_t target_state = 0;
    uint16_t moving_cm = 0;
    uint16_t still_cm = 0;
    std::function<void(int64_t ms, uint8_t *moving_energy, uint8_t *still_energy)> energies;

    // Background noise detection (0x000B): 10 s wait, then the measuring time, then the result state for a while
    enum AutoOutcome { kAutoDone, kAutoFailed, kAutoNeverStarts, kAutoNeverEnds, kAutoEndsSilently };
    AutoOutcome auto_outcome = kAutoDone;
    uint8_t auto_moving[9] = { 45, 40, 30, 25, 18, 12, 10, 10, 10 }; // thresholds the radar sets
    uint8_t auto_still[9] = { 0, 0, 35, 30, 25, 22, 15, 15, 15 };
    int64_t auto_start_ms = -1; // command accepted at
    uint16_t auto_seconds = 0;
    uint16_t auto_status = 0; // 0x001B: 0 not run, 1 running, 2 done

    // Faults
    bool rx_wire_broken = false;      // radar TX -> ESP32 RX cut: nothing reaches the ESP32
    bool tx_wire_broken = false;      // ESP32 TX -> radar RX cut: no command reaches the radar
    std::set<uint16_t> ignore;        // commands never answered
    std::set<uint16_t> refuse;        // commands refused (status 1)
    std::set<uint16_t> answer_late;   // answered after 1.2 s (late ACK, after the ESP32 gave up)
    int miss_next = 0;                // the next n commands are not heard (missed while sending a report)
    bool restart_without_ack = false; // restarts before the ACK of 0x00A3
    bool bluetooth_stuck = false;     // keeps its Bluetooth state across restarts
    int stuck_gate = -1;              // ACKs threshold writes of this gate but keeps the old values
    bool report_silent = false;       // sends no reports (but answers commands)

    RadarSim();
    ~RadarSim();
    // Registers the radar with the simulation (call after sim_reset()).
    void attach();
    // Names of the commands received, e.g. "00FF 0061 00FE"
    std::string command_list() const;
    int count(uint16_t command) const;

private:
    struct Pending {
        int64_t at_ms;
        std::vector<uint8_t> bytes;
    };
    std::vector<Pending> pending_;
    std::vector<uint8_t> rx_; // command bytes received from the ESP32
    int64_t next_report_ms_ = 0;
    int64_t online_at_ms_ = 0;
    uint8_t auto_result_frames_ = 0;
    uint8_t auto_result_state_ = 0;

    void tick();
    void receive(const uint8_t *data, size_t len);
    void handle(uint16_t command, const uint8_t *value, size_t len);
    void ack(uint16_t command, uint16_t status, const std::vector<uint8_t> &value, int delay_ms = 3);
    void send(std::vector<uint8_t> bytes, int delay_ms);
    void restart();
    void send_report();
    uint8_t auto_state();
};
