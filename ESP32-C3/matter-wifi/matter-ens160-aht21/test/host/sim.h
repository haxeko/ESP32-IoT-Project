// Simulated clock, I2C bus and sensors for the host tests: the ESP-IDF / FreeRTOS stubs in stubs/ call into it. Time
// only moves when the code under test waits (vTaskDelay, vTaskDelayUntil); an I2C transfer takes no time.
//
// The AHT21 and the ENS160 behave as their datasheets describe them: the AHT21 measures in measure_ms after the 0xAC
// command and needs its calibration registers initialized when its status is not 0x18; the ENS160 starts in deep
// sleep, measures once a second in standard mode and flags its values as warming up for warm_up_ms.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Aht21Sim {
    bool present = true;
    uint8_t status_after_reset = 0x18; // status after the soft reset: 0x18 ready, else the register init is needed
    uint8_t status = 0x18;
    int measure_ms = 75;         // a measurement takes this long after 0xAC 0x33 0x00
    int64_t busy_until_ms = -1;  // measuring until then
    bool measured = false;       // a measurement was started since the reset
    uint32_t humidity_raw = 0x80000;    // 50 %
    uint32_t temperature_raw = 0x66666; // 29.9999 °C
    bool corrupt_crc = false;
    uint8_t selected_reg = 0;
    std::vector<uint8_t> initialized_regs; // registers written back with 0xB0 | register
    uint8_t reg_value[2] = {0x12, 0x34};   // bytes 1 and 2 returned for a selected register
    bool init_registers = true;            // false: first AHT20 series, calibrated by 0xBE 0x08 0x00 only
    bool calibrates = true;                // false: defective, its status never gets the calibration bit
};

struct Ens160Sim {
    bool present = true;
    uint16_t address = 0x53;
    uint16_t part_id = 0x0160;
    uint8_t opmode = 0x00;          // deep sleep at power-on
    int64_t standard_since_ms = -1; // entered the standard mode at
    int warm_up_ms = 180000;        // values flagged warm-up (or initial start-up) until then
    bool initial_start_up = false;  // a new sensor: validity 2 instead of 1
    bool stuck = false;             // stopped measuring: no new values
    bool error = false;             // STATER
    bool invalid = false;           // validity 3 (invalid output)
    int64_t next_data_ms = 0;
    bool new_data = false;
    bool new_gpr = false;
    uint8_t aqi = 2;
    uint16_t tvoc = 120;
    uint16_t eco2 = 650;
    uint16_t temp_in = 0;
    uint16_t rh_in = 0;
    uint8_t gpr_read[8] = {};
    uint8_t version[3] = {5, 4, 6};
    std::vector<uint8_t> opmode_writes;
    int compensation_writes = 0;
};

struct Sim {
    int64_t now_ms = 0;
    Aht21Sim aht;
    Ens160Sim ens;
    bool bus_created = false;
    int sda = -1;
    int scl = -1;
    bool internal_pullup = false;
    int devices_added = 0;
    int bus_resets = 0;
    std::vector<std::string> i2c; // "<ms> W<addr> <hex bytes>" / "<ms> R<addr> <length>"
    std::vector<std::string> log; // "<ms> <level> <tag>: <message>"
};

extern Sim g_sim;
extern int g_failures;
extern bool g_verbose;

// New simulation at time 0 (sensors present, ENS160 in deep sleep). Drivers keep their static state.
void sim_reset();
int64_t sim_ms();
void sim_advance_ms(uint32_t ms);
void sim_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
bool sim_log_contains(const char *text);
int sim_log_count(const char *text);
// First I2C trace entry containing text at or after index from, -1 if none
int sim_i2c_find(const char *text, size_t from = 0);
int64_t sim_i2c_time(int index);

// 0: ACK
int sim_i2c_probe(uint16_t address);
int sim_i2c_write(uint16_t address, const uint8_t *data, size_t len);
int sim_i2c_read(uint16_t address, uint8_t *data, size_t len);
int sim_i2c_write_read(uint16_t address, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen);

// Frame of the simulated AHT21 (also used to build test frames)
void sim_aht21_frame(uint8_t status, uint32_t humidity_raw, uint32_t temperature_raw, bool corrupt_crc,
                     uint8_t frame[7]);

void sim_parse_args(int argc, char **argv); // -v: print the log
int sim_summary(const char *name);

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            g_failures++;                                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, tolerance)                                                                                    \
    do {                                                                                                               \
        const double check_a_ = (a), check_b_ = (b);                                                                   \
        if (!(check_a_ - check_b_ <= (tolerance) && check_b_ - check_a_ <= (tolerance))) {                            \
            g_failures++;                                                                                              \
            printf("  FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, check_a_, check_b_);                \
        }                                                                                                              \
    } while (0)
