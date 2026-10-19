/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// HLK-LD2410 / LD2410B / LD2410C 24 GHz presence radar on UART1 (256000 8N1): target reports and configuration
// commands (serial protocol V1.0x, LD2410B V1.07, LD2410C V1.09).

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Distance gates 0..8, 0.75 m each at the default resolution
#define LD2410_GATE_COUNT 9
#define LD2410_DEFAULT_GATE_CM 75
// Gate length of the 0.2 m resolution (LD2410B/C)
#define LD2410_FINE_GATE_CM 20
// Accepted range of the farthest detection gate
#define LD2410_MIN_MAX_GATE 2
#define LD2410_MAX_MAX_GATE (LD2410_GATE_COUNT - 1)
// Bluetooth MAC address of the LD2410B/C (HLKRadarTool app)
#define LD2410_MAC_LEN 6
// Highest energy of a gate
#define LD2410_MAX_ENERGY 100

// Target state byte of the reports: 0 none, 1 moving, 2 still, 3 both, and while the radar runs its background
// noise detection (ld2410_start_noise_detection()):
#define LD2410_STATE_NOISE_RUNNING 0x04 // measuring (also during its 10 s wait)
#define LD2410_STATE_NOISE_DONE 0x05    // finished, new thresholds stored
#define LD2410_STATE_NOISE_FAILED 0x06  // failed

// Latest target report.
typedef struct {
    uint8_t state;         // target state byte, see above
    bool moving;           // a moving target is detected (state 1 or 3)
    bool still;            // a still (stationary) target is detected (state 2 or 3)
    uint16_t moving_cm;    // distance of the moving target
    uint16_t still_cm;     // distance of the still target
    uint16_t detection_cm; // "detection distance" reported by the radar
    int64_t timestamp_ms;  // esp_timer time of the last valid report
} ld2410_target_t;

// Per-gate sensitivity thresholds (energy 0-100): a target is reported when its energy at that gate is above the
// threshold, 100 ignores the gate.
typedef struct {
    uint8_t moving[LD2410_GATE_COUNT];
    uint8_t still[LD2410_GATE_COUNT];
} ld2410_thresholds_t;

// Firmware version of the radar (command 0x00A0): V2.44.25070917 is major 0x0244, minor 0x25070917.
typedef struct {
    uint16_t type;
    uint16_t major;
    uint32_t minor;
} ld2410_version_t;

// Parameters stored in the radar (command 0x0061).
typedef struct {
    ld2410_thresholds_t thresholds;
    uint8_t max_moving_gate;    // farthest gate for moving targets
    uint8_t max_still_gate;     // farthest gate for still targets
    uint16_t no_one_duration_s; // "unmanned duration": a lost target is still reported for this long
} ld2410_config_t;

// One report as received, with the energy of every gate when the radar is in engineering mode.
typedef struct {
    ld2410_target_t target;
    bool engineering;     // engineering mode: the energies below are valid
    uint8_t moving_gates; // energies of gates 0..moving_gates-1
    uint8_t still_gates;  // energies of gates 0..still_gates-1
    uint8_t moving_energy[LD2410_GATE_COUNT];
    uint8_t still_energy[LD2410_GATE_COUNT];
} ld2410_report_t;

// Called on the reader task for every valid report, once ld2410_get_target() returns it. It runs while the UART is
// read, so it must be short and must not block.
typedef void (*ld2410_report_hook_t)(const ld2410_report_t *report);

// Engineering reports counted per gate and energy while capturing, about 3.6 KB: allocate it only for a capture.
// Energies above LD2410_MAX_ENERGY are counted as LD2410_MAX_ENERGY, a count stops at UINT16_MAX.
typedef struct {
    uint16_t moving[LD2410_GATE_COUNT][LD2410_MAX_ENERGY + 1];
    uint16_t still[LD2410_GATE_COUNT][LD2410_MAX_ENERGY + 1];
    uint32_t frames;
} ld2410_capture_t;

// Starts UART1 and the reader task (priority 5).
esp_err_t ld2410_init(int rx_pin, int tx_pin);

// Parses bytes received from the radar. Called by the reader task; the host tests feed their bytes here.
void ld2410_feed(const uint8_t *data, size_t len);

// Returns false while no valid report has been received.
bool ld2410_get_target(ld2410_target_t *out);

// Sets the report hook (nullptr: none).
void ld2410_set_report_hook(ld2410_report_hook_t hook);

// Configuration commands. Each one waits up to 1 s for the radar's ACK, so call them from a normal task, never
// from the Matter task or an esp_timer callback. The other commands are only accepted between config_begin and
// config_end (each tried 3 times: a radar left in configuration mode sends no reports).
esp_err_t ld2410_config_begin(void);
esp_err_t ld2410_config_end(void);
// Engineering mode adds the energy of every gate to the reports. It is off after the radar powers up.
esp_err_t ld2410_set_engineering_mode(bool enable);
esp_err_t ld2410_read_config(ld2410_config_t *out);
esp_err_t ld2410_read_thresholds(ld2410_thresholds_t *out);
// Stored in the radar's flash. The still thresholds of gates 0 and 1 cannot be set (the radar ignores them).
esp_err_t ld2410_set_gate_thresholds(uint8_t gate, uint8_t moving, uint8_t still);
// Farthest gates (LD2410_MIN_MAX_GATE..LD2410_MAX_MAX_GATE) and no-one duration, stored in the radar's flash.
esp_err_t ld2410_set_max_gates_and_duration(uint8_t max_moving_gate, uint8_t max_still_gate,
                                            uint16_t no_one_duration_s);
// Length of one gate in cm: 75, or 20 when an LD2410B/C uses the 0.2 m resolution. Radars without this query
// (older firmware) refuse it or do not answer it, and use 75 cm.
esp_err_t ld2410_read_gate_length(uint16_t *gate_cm);
// Stores the gate length (LD2410_DEFAULT_GATE_CM or LD2410_FINE_GATE_CM); used after ld2410_restart().
esp_err_t ld2410_set_gate_length(uint16_t gate_cm);
esp_err_t ld2410_read_firmware_version(ld2410_version_t *out);
// "V2.44.25070917"
void ld2410_format_version(const ld2410_version_t *version, char *buf, size_t len);
// Background noise detection of the LD2410B/C (newer firmware, command 0x000B): the radar waits 10 s, measures
// every gate for `seconds`, then stores new moving and still thresholds itself. The reports carry
// LD2410_STATE_NOISE_* meanwhile. Radars without it refuse the command or do not answer it.
esp_err_t ld2410_start_noise_detection(uint16_t seconds);
// 0 not running, 1 running, 2 finished (command 0x001B)
esp_err_t ld2410_read_noise_detection_status(uint16_t *status);
// Restarts the radar, which leaves the configuration mode and answers commands again about a second later. The
// radar may restart before its ACK is sent: only a refusal (ESP_FAIL) is an error.
esp_err_t ld2410_restart(void);
// Bluetooth of the LD2410B/C, read from its MAC address query: the radar answers a placeholder address while its
// Bluetooth is off. mac (optional) receives the address when *enabled. Radars without Bluetooth refuse the query or
// do not answer it.
esp_err_t ld2410_read_bluetooth(bool *enabled, uint8_t mac[LD2410_MAC_LEN]);
// Stored in the radar's flash; used after ld2410_restart().
esp_err_t ld2410_set_bluetooth(bool enable);

// Between start and stop, every engineering report is counted in *capture, which start clears. It must stay
// allocated until ld2410_capture_stop().
void ld2410_capture_start(ld2410_capture_t *capture);
void ld2410_capture_stop(void);
