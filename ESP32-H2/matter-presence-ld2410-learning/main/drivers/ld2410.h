/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Distance gates 0..8, 0.75 m each at the default resolution
#define LD2410_GATE_COUNT 9
#define LD2410_DEFAULT_GATE_CM 75
// Gate length of the 0.2 m resolution (LD2410B/C)
#define LD2410_FINE_GATE_CM 20
// Accepted range of the farthest detection gate
#define LD2410_MIN_MAX_GATE 2
#define LD2410_MAX_MAX_GATE (LD2410_GATE_COUNT - 1)
// Bluetooth of the LD2410B/C (HLKRadarTool app)
#define LD2410_MAC_LEN 6
#define LD2410_BT_PASSWORD_LEN 6

// Latest target report from the HLK-LD2410 24 GHz presence radar.
typedef struct {
    bool moving;            // a moving target is detected
    bool still;             // a still (stationary) target is detected
    uint16_t moving_cm;     // distance of the moving target
    uint16_t still_cm;      // distance of the still target
    uint16_t detection_cm;  // "detection distance" reported by the radar
    int64_t timestamp_ms;   // esp_timer time of the last valid frame
} ld2410_target_t;

// Per-gate sensitivity thresholds (energy 0-100): a target is reported when
// its energy at that gate is above the threshold, 100 ignores the gate.
typedef struct {
    uint8_t moving[LD2410_GATE_COUNT];
    uint8_t still[LD2410_GATE_COUNT];
} ld2410_thresholds_t;

// Parameters stored in the radar.
typedef struct {
    ld2410_thresholds_t thresholds;
    uint8_t max_moving_gate;    // farthest gate for moving targets
    uint8_t max_still_gate;     // farthest gate for still targets
    uint16_t no_one_duration_s; // "unmanned duration": a lost target is still reported for this long
} ld2410_config_t;

// Highest per-gate energies seen in engineering frames while capturing.
typedef struct {
    uint8_t moving[LD2410_GATE_COUNT];
    uint8_t still[LD2410_GATE_COUNT];
    uint32_t frames;
} ld2410_capture_t;

esp_err_t ld2410_init(int rx_pin, int tx_pin);

// Returns false when no valid frame has been received yet.
bool ld2410_get_target(ld2410_target_t *out);

// Configuration commands. Each one waits for the radar's ACK, so call them
// from a normal task, never from the Matter task or an esp_timer callback.
// The other commands are only accepted between config_begin and config_end;
// the radar resumes its normal work after config_end.
esp_err_t ld2410_config_begin(void);
esp_err_t ld2410_config_end(void);
// Engineering mode adds the energy of every gate to the target reports. It is
// off after the radar powers up.
esp_err_t ld2410_set_engineering_mode(bool enable);
esp_err_t ld2410_read_config(ld2410_config_t *out);
esp_err_t ld2410_read_thresholds(ld2410_thresholds_t *out);
// Stored in the radar's flash, kept across power cycles. The still thresholds
// of gates 0 and 1 cannot be set (the radar ignores them).
esp_err_t ld2410_set_gate_thresholds(uint8_t gate, uint8_t moving, uint8_t still);
// Farthest gates (LD2410_MIN_MAX_GATE..LD2410_MAX_MAX_GATE) and no-one
// duration, stored in the radar's flash.
esp_err_t ld2410_set_max_gates_and_duration(uint8_t max_moving_gate, uint8_t max_still_gate,
                                            uint16_t no_one_duration_s);
// Length of one gate in cm: 75, or 20 when an LD2410B/C is set to the 0.2 m
// resolution. Radars without this query (older firmware) return an error and
// use 75 cm.
esp_err_t ld2410_read_gate_length(uint16_t *gate_cm);
// Stores the gate length (LD2410_DEFAULT_GATE_CM or LD2410_FINE_GATE_CM) in
// the radar's flash; it takes effect after ld2410_restart().
esp_err_t ld2410_set_gate_length(uint16_t gate_cm);
// Restarts the radar, which leaves the configuration mode; it answers
// commands again about a second later.
esp_err_t ld2410_restart(void);
// Bluetooth of the LD2410B/C, read from its MAC address query: the radar
// answers a placeholder address while its Bluetooth is off. mac (optional)
// receives the address when *enabled. Radars without Bluetooth refuse the
// query or do not answer it.
esp_err_t ld2410_read_bluetooth(bool *enabled, uint8_t mac[LD2410_MAC_LEN]);
// Stored in the radar's flash; takes effect after ld2410_restart().
esp_err_t ld2410_set_bluetooth(bool enable);
// Password the HLKRadarTool app asks for: exactly LD2410_BT_PASSWORD_LEN
// printable ASCII characters (factory default "HiLink"). Stored in the
// radar's flash, takes effect after ld2410_restart(). It cannot be read back.
esp_err_t ld2410_set_bluetooth_password(const char *password);

// Between start and stop, engineering frames update the per-gate maxima.
void ld2410_capture_start(void);
void ld2410_capture_stop(ld2410_capture_t *out);
