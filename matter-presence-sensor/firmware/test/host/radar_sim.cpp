#include "radar_sim.h"

#include <stdio.h>
#include <string.h>

#include "ld2410.h"
#include "sim.h"

static constexpr int kReportMs = 100;
static constexpr int kRestartOfflineMs = 600; // the ESP32 waits 1 s after a restart
static constexpr int kLateMs = 1002;          // just after the ESP32 gave up waiting (1 s)
static constexpr int kAutoLeaveMs = 10 * 1000;
static constexpr int kAutoResultFrames = 5;

RadarSim::RadarSim() {}

RadarSim::~RadarSim()
{
    g_sim.on_uart_write = nullptr;
    g_sim.hooks.clear();
}

void RadarSim::attach()
{
    g_sim.on_uart_write = [this](const uint8_t *data, size_t len) { receive(data, len); };
    g_sim.hooks.push_back([this]() { tick(); });
    // A new simulation (e.g. the ESP32 rebooted): the radar keeps its settings and state, nothing is in transit
    pending_.clear();
    rx_.clear();
    next_report_ms_ = sim_ms() + kReportMs;
    online_at_ms_ = sim_ms();
}

std::string RadarSim::command_list() const
{
    std::string s;
    for (uint16_t c : commands) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%s%04X", s.empty() ? "" : " ", c);
        s += buf;
    }
    return s;
}

int RadarSim::count(uint16_t command) const
{
    int n = 0;
    for (uint16_t c : commands) {
        n += c == command;
    }
    return n;
}

void RadarSim::send(std::vector<uint8_t> bytes, int delay_ms)
{
    pending_.push_back({ sim_ms() + delay_ms, bytes });
}

void RadarSim::ack(uint16_t command, uint16_t status, const std::vector<uint8_t> &value, int delay_ms)
{
    const uint16_t word = command | 0x0100;
    const size_t len = 4 + value.size();
    std::vector<uint8_t> f = { 0xFD, 0xFC, 0xFB, 0xFA, (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)word,
                               (uint8_t)(word >> 8), (uint8_t)status, (uint8_t)(status >> 8) };
    f.insert(f.end(), value.begin(), value.end());
    f.insert(f.end(), { 0x04, 0x03, 0x02, 0x01 });
    send(f, answer_late.count(command) ? kLateMs : delay_ms);
}

void RadarSim::restart()
{
    restarts++;
    offline = true;
    online_at_ms_ = sim_ms() + kRestartOfflineMs;
    config_mode = false;
    engineering = false;
    resolution = stored_resolution;
    if (!bluetooth_stuck) {
        bluetooth = stored_bluetooth;
    } else {
        stored_bluetooth = bluetooth;
    }
    if (auto_start_ms >= 0) {
        auto_start_ms = -1; // detection stopped
        auto_status = 0;
    }
    auto_result_frames_ = 0;
}

void RadarSim::receive(const uint8_t *data, size_t len)
{
    rx_.insert(rx_.end(), data, data + len);
    while (rx_.size() >= 10) {
        if (!(rx_[0] == 0xFD && rx_[1] == 0xFC && rx_[2] == 0xFB && rx_[3] == 0xFA)) {
            rx_.erase(rx_.begin());
            continue;
        }
        const size_t data_len = rx_[4] | (rx_[5] << 8);
        if (rx_.size() < 6 + data_len + 4) {
            return;
        }
        const uint8_t *footer = &rx_[6 + data_len];
        const bool ok = footer[0] == 0x04 && footer[1] == 0x03 && footer[2] == 0x02 && footer[3] == 0x01 && data_len >= 2;
        const uint16_t command = rx_[6] | (rx_[7] << 8);
        std::vector<uint8_t> value(rx_.begin() + 8, rx_.begin() + 6 + data_len);
        rx_.erase(rx_.begin(), rx_.begin() + 6 + data_len + 4);
        if (!ok || tx_wire_broken || offline) {
            continue;
        }
        if (miss_next > 0) {
            miss_next--;
            continue;
        }
        commands.push_back(command);
        handle(command, value.data(), value.size());
    }
}

static uint32_t word_value(const uint8_t *v)
{
    return v[2] | (v[3] << 8) | (v[4] << 16) | ((uint32_t)v[5] << 24);
}

void RadarSim::handle(uint16_t command, const uint8_t *value, size_t len)
{
    if (ignore.count(command)) {
        return;
    }
    if (refuse.count(command)) {
        ack(command, 1, {});
        return;
    }
    if (!config_mode && command != 0x00FF) {
        return; // only accepted in configuration mode
    }
    auto unsupported = [&]() {
        if (refuse_unsupported) {
            ack(command, 1, {});
        }
    };
    switch (command) {
    case 0x00FF:
        config_mode = true;
        ack(command, 0, { 0x01, 0x00, 0x40, 0x00 });
        break;
    case 0x00FE:
        config_mode = false;
        next_report_ms_ = sim_ms() + kReportMs;
        ack(command, 0, {});
        break;
    case 0x0060: {
        uint8_t mov = max_moving_gate, sti = max_still_gate;
        uint16_t dur = no_one_duration;
        for (size_t i = 0; i + 6 <= len; i += 6) {
            const uint16_t word = value[i] | (value[i + 1] << 8);
            const uint32_t v = word_value(&value[i]);
            if (word == 0) {
                mov = (uint8_t)v;
            } else if (word == 1) {
                sti = (uint8_t)v;
            } else if (word == 2) {
                dur = (uint16_t)v;
            }
        }
        if (mov < 2 || mov > 8 || sti < 2 || sti > 8) {
            ack(command, 1, {});
            break;
        }
        max_moving_gate = mov;
        max_still_gate = sti;
        no_one_duration = dur;
        ack(command, 0, {});
        break;
    }
    case 0x0061: {
        std::vector<uint8_t> v = { 0xAA, 8, max_moving_gate, max_still_gate };
        v.insert(v.end(), moving, moving + 9);
        v.insert(v.end(), still, still + 9);
        v.push_back((uint8_t)no_one_duration);
        v.push_back((uint8_t)(no_one_duration >> 8));
        ack(command, 0, v);
        break;
    }
    case 0x0062:
    case 0x0063:
        engineering = command == 0x0062;
        ack(command, 0, {});
        break;
    case 0x0064: {
        if (len < 18) {
            ack(command, 1, {});
            break;
        }
        const uint32_t gate = word_value(&value[0]);
        const uint32_t mv = word_value(&value[6]);
        const uint32_t sv = word_value(&value[12]);
        if (gate > 8 || mv > 100 || sv > 100) {
            ack(command, 1, {});
            break;
        }
        if ((int)gate != stuck_gate) {
            moving[gate] = (uint8_t)mv;
            if (gate >= 2) {
                still[gate] = (uint8_t)sv; // gates 0 and 1 have no settable still threshold
            }
        }
        ack(command, 0, {});
        break;
    }
    case 0x00A0:
        ack(command, 0, { 0x01, 0x00, 0x44, 0x02, 0x17, 0x09, 0x07, 0x25 }); // V2.44.25070917
        break;
    case 0x00A3:
        if (restart_without_ack) {
            restart();
        } else {
            ack(command, 0, {}, 1);
            sim_at(sim_ms() + 2, [this]() { restart(); });
        }
        break;
    case 0x00A4:
        if (!has_bluetooth) {
            unsupported();
            break;
        }
        stored_bluetooth = value[0] != 0;
        ack(command, 0, {});
        break;
    case 0x00A5:
        if (!has_bluetooth) {
            unsupported();
            break;
        }
        if (bluetooth) {
            ack(command, 0, std::vector<uint8_t>(mac, mac + 6));
        } else {
            ack(command, 0, { 0x08, 0x05, 0x04, 0x03, 0x02, 0x01 });
        }
        break;
    case 0x00AA:
        if (!has_resolution) {
            unsupported();
            break;
        }
        stored_resolution = value[0];
        ack(command, 0, {});
        break;
    case 0x00AB:
        if (!has_resolution) {
            unsupported();
            break;
        }
        ack(command, 0, { (uint8_t)resolution, 0x00 });
        break;
    case 0x000B:
        if (!has_auto) {
            unsupported();
            break;
        }
        auto_seconds = value[0] | (value[1] << 8);
        auto_start_ms = sim_ms();
        auto_status = auto_outcome == kAutoNeverStarts ? 0 : 1;
        ack(command, 0, {});
        break;
    case 0x001B:
        if (!has_auto) {
            unsupported();
            break;
        }
        ack(command, 0, { (uint8_t)auto_status, 0x00 });
        break;
    default:
        unsupported();
        break;
    }
}

// State reported while the background noise detection runs or just ended, 0xFF otherwise.
uint8_t RadarSim::auto_state()
{
    if (auto_result_frames_ > 0) {
        auto_result_frames_--;
        return auto_result_state_;
    }
    if (auto_start_ms < 0 || auto_outcome == kAutoNeverStarts) {
        return 0xFF;
    }
    const int64_t end_ms = auto_start_ms + kAutoLeaveMs + (int64_t)auto_seconds * 1000;
    if (sim_ms() < end_ms || auto_outcome == kAutoNeverEnds) {
        return LD2410_STATE_NOISE_RUNNING;
    }
    auto_start_ms = -1;
    if (auto_outcome == kAutoFailed) {
        auto_status = 0;
        auto_result_state_ = LD2410_STATE_NOISE_FAILED;
    } else {
        memcpy(moving, auto_moving, sizeof(moving));
        for (int g = 2; g < 9; g++) {
            still[g] = auto_still[g];
        }
        auto_status = 2;
        auto_result_state_ = LD2410_STATE_NOISE_DONE;
        if (auto_outcome == kAutoEndsSilently) {
            return 0xFF;
        }
    }
    auto_result_frames_ = kAutoResultFrames - 1;
    return auto_result_state_;
}

void RadarSim::send_report()
{
    const uint8_t override_state = auto_state();
    const uint8_t state = override_state != 0xFF ? override_state : target_state;
    std::vector<uint8_t> p = { (uint8_t)(engineering ? 0x01 : 0x02), 0xAA, state, (uint8_t)moving_cm,
                               (uint8_t)(moving_cm >> 8), 60, (uint8_t)still_cm, (uint8_t)(still_cm >> 8), 40, 0, 0 };
    if (engineering) {
        uint8_t me[9] = { 5, 5, 5, 5, 5, 5, 5, 5, 5 };
        uint8_t se[9] = { 5, 5, 5, 5, 5, 5, 5, 5, 5 };
        if (energies) {
            energies(sim_ms(), me, se);
        }
        p.push_back(8);
        p.push_back(8);
        p.insert(p.end(), me, me + 9);
        p.insert(p.end(), se, se + 9);
        p.push_back(0x7F); // light sensor
        p.push_back(0x00); // OUT pin
    }
    p.push_back(0x55);
    p.push_back(0x00);
    std::vector<uint8_t> f = { 0xF4, 0xF3, 0xF2, 0xF1, (uint8_t)p.size(), 0x00 };
    f.insert(f.end(), p.begin(), p.end());
    f.insert(f.end(), { 0xF8, 0xF7, 0xF6, 0xF5 });
    send(f, 0);
}

void RadarSim::tick()
{
    if (offline && sim_ms() >= online_at_ms_) {
        offline = false;
        next_report_ms_ = sim_ms() + kReportMs;
    }
    if (!offline && !config_mode && !report_silent && sim_ms() >= next_report_ms_) {
        next_report_ms_ += kReportMs;
        send_report();
    }
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].at_ms <= sim_ms()) {
            const std::vector<uint8_t> bytes = pending_[i].bytes;
            pending_.erase(pending_.begin() + i);
            if (!rx_wire_broken) {
                ld2410_feed(bytes.data(), bytes.size());
            }
        } else {
            i++;
        }
    }
}
