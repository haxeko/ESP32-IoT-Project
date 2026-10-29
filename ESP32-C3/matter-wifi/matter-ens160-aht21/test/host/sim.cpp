// See sim.h.
#include "sim.h"

#include <stdarg.h>
#include <string.h>

#include "sensor_math.h"

Sim g_sim;
int g_failures;
bool g_verbose;

void sim_reset()
{
    g_sim = Sim();
}

int64_t sim_ms()
{
    return g_sim.now_ms;
}

void sim_advance_ms(uint32_t ms)
{
    g_sim.now_ms += ms;
}

void sim_log(char level, const char *tag, const char *fmt, ...)
{
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    char line[600];
    snprintf(line, sizeof(line), "%lld %c %s: %s", (long long)g_sim.now_ms, level, tag, msg);
    g_sim.log.push_back(line);
    if (g_verbose) {
        printf("    %s\n", line);
    }
}

bool sim_log_contains(const char *text)
{
    return sim_log_count(text) > 0;
}

int sim_log_count(const char *text)
{
    int count = 0;
    for (const std::string &line : g_sim.log) {
        if (line.find(text) != std::string::npos) {
            count++;
        }
    }
    return count;
}

int sim_i2c_find(const char *text, size_t from)
{
    for (size_t i = from; i < g_sim.i2c.size(); i++) {
        if (g_sim.i2c[i].find(text) != std::string::npos) {
            return (int)i;
        }
    }
    return -1;
}

int64_t sim_i2c_time(int index)
{
    return index < 0 ? -1 : atoll(g_sim.i2c[index].c_str());
}

static void trace(char dir, uint16_t address, const uint8_t *data, size_t len)
{
    char line[200];
    int n = snprintf(line, sizeof(line), "%lld %c%02X", (long long)g_sim.now_ms, dir, address);
    if (dir == 'W') {
        n += snprintf(line + n, sizeof(line) - n, " ");
        for (size_t i = 0; i < len && n < (int)sizeof(line) - 3; i++) {
            n += snprintf(line + n, sizeof(line) - n, "%02X", data[i]);
        }
    } else {
        snprintf(line + n, sizeof(line) - n, " %zu", len);
    }
    g_sim.i2c.push_back(line);
}

// ---------------------------------------------------------------------------------------------------------------------
// AHT21

void sim_aht21_frame(uint8_t status, uint32_t humidity_raw, uint32_t temperature_raw, bool corrupt_crc,
                     uint8_t frame[7])
{
    frame[0] = status;
    frame[1] = (uint8_t)(humidity_raw >> 12);
    frame[2] = (uint8_t)(humidity_raw >> 4);
    frame[3] = (uint8_t)(((humidity_raw & 0x0F) << 4) | ((temperature_raw >> 16) & 0x0F));
    frame[4] = (uint8_t)(temperature_raw >> 8);
    frame[5] = (uint8_t)temperature_raw;
    frame[6] = aht2x_crc8(frame, 6) ^ (corrupt_crc ? 0x55 : 0x00);
}

static uint8_t aht_status()
{
    const Aht21Sim &a = g_sim.aht;
    const bool busy = a.busy_until_ms >= 0 && g_sim.now_ms < a.busy_until_ms;
    return (uint8_t)(a.status | (busy ? 0x80 : 0x00));
}

static int aht_write(const uint8_t *d, size_t len)
{
    Aht21Sim &a = g_sim.aht;
    if (len == 1 && d[0] == 0xBA) { // soft reset
        a.status = a.status_after_reset;
        a.busy_until_ms = -1;
        a.measured = false;
        a.initialized_regs.clear();
        return 0;
    }
    if (len == 3 && d[0] == 0xAC && d[1] == 0x33 && d[2] == 0x00) {
        a.busy_until_ms = g_sim.now_ms + a.measure_ms;
        a.measured = true;
        return 0;
    }
    if (len == 3 && (d[0] == 0x1B || d[0] == 0x1C || d[0] == 0x1E) && d[1] == 0 && d[2] == 0) {
        a.selected_reg = d[0];
        return 0;
    }
    if (len == 3 && d[0] == 0xBE && d[1] == 0x08 && d[2] == 0x00 && a.selected_reg != 0x1E) {
        if (a.calibrates) {
            a.status |= 0x08; // calibration command of the first AHT20 series
        }
        return 0;
    }
    if (len == 3 && (d[0] == 0xBB || d[0] == 0xBC || d[0] == 0xBE)) {
        const uint8_t reg = (uint8_t)(0x10 | (d[0] & 0x0F));
        if (a.init_registers && a.calibrates && reg == a.selected_reg && d[1] == a.reg_value[0] &&
            d[2] == a.reg_value[1]) {
            a.initialized_regs.push_back(reg);
        }
        a.selected_reg = 0;
        if (a.initialized_regs.size() == 3) {
            a.status |= 0x18;
        }
        return 0;
    }
    return -1; // unknown command: NACK
}

static int aht_read(uint8_t *d, size_t len)
{
    const Aht21Sim &a = g_sim.aht;
    if (len == 1) {
        d[0] = aht_status();
        return 0;
    }
    if (len == 3) {
        d[0] = aht_status();
        d[1] = a.reg_value[0];
        d[2] = a.reg_value[1];
        return 0;
    }
    if (len == 7) {
        if (a.measured) {
            sim_aht21_frame(aht_status(), a.humidity_raw, a.temperature_raw, a.corrupt_crc, d);
        } else {
            sim_aht21_frame(aht_status(), 0, 0, false, d);
        }
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// ENS160

static void ens_update()
{
    Ens160Sim &e = g_sim.ens;
    if (e.opmode != 0x02 || e.stuck) {
        return;
    }
    while (g_sim.now_ms >= e.next_data_ms) {
        e.new_data = true;
        e.next_data_ms += 1000;
    }
}

static uint8_t ens_status()
{
    Ens160Sim &e = g_sim.ens;
    ens_update();
    uint8_t s = 0;
    if (e.opmode == 0x02) {
        s |= 0x80;
        if (e.invalid) {
            s |= 3 << 2;
        } else if (g_sim.now_ms - e.standard_since_ms < e.warm_up_ms) {
            s |= (e.initial_start_up ? 2 : 1) << 2;
        }
    }
    if (e.error) {
        s |= 0x40;
    }
    if (e.new_data) {
        s |= 0x02;
    }
    if (e.new_gpr) {
        s |= 0x01;
    }
    return s;
}

static uint8_t ens_reg(uint8_t reg)
{
    Ens160Sim &e = g_sim.ens;
    switch (reg) {
    case 0x00:
        return (uint8_t)e.part_id;
    case 0x01:
        return (uint8_t)(e.part_id >> 8);
    case 0x10:
        return e.opmode;
    case 0x13:
        return (uint8_t)e.temp_in;
    case 0x14:
        return (uint8_t)(e.temp_in >> 8);
    case 0x15:
        return (uint8_t)e.rh_in;
    case 0x16:
        return (uint8_t)(e.rh_in >> 8);
    case 0x20:
        return ens_status();
    case 0x21:
        return e.aqi;
    case 0x22:
        return (uint8_t)e.tvoc;
    case 0x23:
        return (uint8_t)(e.tvoc >> 8);
    case 0x24:
        return (uint8_t)e.eco2;
    case 0x25:
        return (uint8_t)(e.eco2 >> 8);
    default:
        if (reg >= 0x48 && reg < 0x50) {
            return e.gpr_read[reg - 0x48];
        }
        return 0;
    }
}

static void ens_write_reg(uint8_t reg, uint8_t value)
{
    Ens160Sim &e = g_sim.ens;
    switch (reg) {
    case 0x10:
        e.opmode_writes.push_back(value);
        if (value == 0xF0) { // reset: back to deep sleep, compensation lost; also ends a hang or an error
            e.opmode = 0x00;
            e.temp_in = 0;
            e.rh_in = 0;
            e.standard_since_ms = -1;
            e.new_data = false;
            e.error = false;
            e.stuck = false;
        } else {
            if (value == 0x02 && e.opmode != 0x02) {
                e.standard_since_ms = g_sim.now_ms;
                e.next_data_ms = g_sim.now_ms + 1000;
                e.new_data = false;
            }
            e.opmode = value;
        }
        break;
    case 0x12:
        if (e.opmode != 0x01) {
            break; // commands only in idle mode
        }
        if (value == 0x0E) {
            memcpy(&e.gpr_read[4], e.version, 3);
            e.new_gpr = true;
        } else if (value == 0xCC) {
            memset(e.gpr_read, 0, sizeof(e.gpr_read));
        }
        break;
    case 0x13:
        e.temp_in = (uint16_t)((e.temp_in & 0xFF00) | value);
        e.compensation_writes++;
        break;
    case 0x14:
        e.temp_in = (uint16_t)((e.temp_in & 0x00FF) | (value << 8));
        break;
    case 0x15:
        e.rh_in = (uint16_t)((e.rh_in & 0xFF00) | value);
        break;
    case 0x16:
        e.rh_in = (uint16_t)((e.rh_in & 0x00FF) | (value << 8));
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Bus

int sim_i2c_probe(uint16_t address)
{
    if (address == 0x38 && g_sim.aht.present) {
        return 0;
    }
    if (address == g_sim.ens.address && g_sim.ens.present) {
        return 0;
    }
    return -1;
}

int sim_i2c_write(uint16_t address, const uint8_t *data, size_t len)
{
    trace('W', address, data, len);
    if (sim_i2c_probe(address) != 0 || len == 0) {
        return -1;
    }
    if (address == 0x38) {
        return aht_write(data, len);
    }
    for (size_t i = 1; i < len; i++) {
        ens_write_reg((uint8_t)(data[0] + i - 1), data[i]);
    }
    return 0;
}

int sim_i2c_read(uint16_t address, uint8_t *data, size_t len)
{
    trace('R', address, data, len);
    if (sim_i2c_probe(address) != 0) {
        return -1;
    }
    if (address == 0x38) {
        return aht_read(data, len);
    }
    return -1; // the ENS160 is always read through a register address
}

int sim_i2c_write_read(uint16_t address, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen)
{
    trace('W', address, wdata, wlen);
    trace('R', address, rdata, rlen);
    if (sim_i2c_probe(address) != 0 || address == 0x38 || wlen != 1) {
        return -1;
    }
    bool data_read = false;
    for (size_t i = 0; i < rlen; i++) {
        const uint8_t reg = (uint8_t)(wdata[0] + i);
        rdata[i] = ens_reg(reg);
        data_read |= reg >= 0x21 && reg <= 0x25;
        if (reg >= 0x48 && reg < 0x50) {
            g_sim.ens.new_gpr = false;
        }
    }
    if (data_read) {
        g_sim.ens.new_data = false; // NEWDAT clears at the first DATA_x read
    }
    return 0;
}

static const char *g_program;

void sim_parse_args(int argc, char **argv)
{
    g_program = argc > 0 ? argv[0] : nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        }
    }
}

int sim_summary(const char *name)
{
    // The binary name tells apart the builds of one test with other options
    if (g_program != nullptr) {
        const char *slash = strrchr(g_program, '/');
        name = slash != nullptr ? slash + 1 : g_program;
    }
    if (g_failures == 0) {
        printf("%s: OK\n", name);
        return 0;
    }
    printf("%s: %d FAILED\n", name, g_failures);
    return 1;
}
