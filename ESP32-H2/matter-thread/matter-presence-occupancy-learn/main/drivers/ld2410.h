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

// Distance gates 0..8 (0.75 m each at the default resolution)
#define LD2410_GATE_COUNT 9

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
esp_err_t ld2410_read_thresholds(ld2410_thresholds_t *out);
// Stored in the radar's flash, kept across power cycles. The still thresholds
// of gates 0 and 1 cannot be set (the radar ignores them).
esp_err_t ld2410_set_gate_thresholds(uint8_t gate, uint8_t moving, uint8_t still);

// Between start and stop, engineering frames update the per-gate maxima.
void ld2410_capture_start(void);
void ld2410_capture_stop(ld2410_capture_t *out);
