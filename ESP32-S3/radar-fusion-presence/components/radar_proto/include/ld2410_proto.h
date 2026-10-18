/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// HLK-LD2410 / LD2410B / LD2410C report and command payloads.
// Sources: Hi-Link "LD2410 Serial Communication Protocol V1.02", "LD2410B Serial
// communication protocol V1.07", "HLK-LD2410C Serial communication protocol"
// (revisions up to V1.08); see docs/phase1/giao-thuc-ld2410.md.

#include "hlk_frame.h"

// Distance gates 0..8: 0.75 m each by default, 0.2 m at the fine resolution (LD2410B/C)
#define LD2410_GATES 9
#define LD2410_MAX_ENERGY 100

// Report types
#define LD2410_TYPE_ENGINEERING 0x01
#define LD2410_TYPE_BASIC 0x02

// Target state byte
#define LD2410_STATE_NONE 0x00
#define LD2410_STATE_MOVING 0x01
#define LD2410_STATE_STILL 0x02
#define LD2410_STATE_BOTH 0x03
#define LD2410_STATE_NOISE_RUNNING 0x04 // background noise detection (command 0x000B) running
#define LD2410_STATE_NOISE_DONE 0x05
#define LD2410_STATE_NOISE_FAILED 0x06

// LD2410-only command words
#define LD2410_CMD_SET_MAX_GATES 0x0060   // max moving gate, max still gate (2..8), no-one duration (s)
#define LD2410_CMD_READ_PARAMS 0x0061
#define LD2410_CMD_ENGINEERING_ON 0x0062  // lost at power off
#define LD2410_CMD_ENGINEERING_OFF 0x0063
#define LD2410_CMD_SET_SENSITIVITY 0x0064 // per gate (or gate 0xFFFF = all) moving and still threshold
#define LD2410_CMD_SET_RESOLUTION 0x00AA  // B/C: 0 = 0.75 m, 1 = 0.2 m per gate, effective after a restart
#define LD2410_CMD_READ_RESOLUTION 0x00AB
#define LD2410_CMD_SET_AUX_CONTROL 0x00AD // B/C V1.06+: light sensor control of the OUT pin
#define LD2410_CMD_READ_AUX_CONTROL 0x00AE
#define LD2410_CMD_NOISE_DETECTION 0x000B // B/C V1.07+ (firmware V2.44): measure background, set thresholds
#define LD2410_CMD_NOISE_STATUS 0x001B    // 0 idle, 1 running, 2 done

typedef struct {
    uint8_t type;  // LD2410_TYPE_*
    uint8_t state; // LD2410_STATE_*
    uint16_t moving_cm;
    uint8_t moving_energy;
    uint16_t still_cm;
    uint8_t still_energy;
    uint16_t detection_cm;
    // Engineering reports only (0 / unset in basic reports):
    uint8_t moving_gates; // energies sent: "maximum moving distance gate N" + 1
    uint8_t still_gates;
    uint8_t moving_gate_energy[LD2410_GATES];
    uint8_t still_gate_energy[LD2410_GATES];
    // Bytes after the gate energies: "retain data" in the LD2410 and early B/C
    // documents; light sensor value (0-255) and OUT pin state (0/1) from LD2410B
    // V1.07 / LD2410C V1.06 on.
    uint8_t extra_len;
    uint8_t light;
    uint8_t out_pin;
} ld2410_report_t;

// Decodes the payload of an HLK_FRAME_LD2410_REPORT frame.
hlk_err_t ld2410_decode_report(const uint8_t *payload, size_t len, ld2410_report_t *out);

// Reply of LD2410_CMD_READ_PARAMS
typedef struct {
    uint8_t gates;           // "maximum distance gate N" + 1 (9)
    uint8_t max_moving_gate; // configured farthest moving gate
    uint8_t max_still_gate;
    uint8_t moving_threshold[LD2410_GATES];
    uint8_t still_threshold[LD2410_GATES];
    uint16_t no_one_duration_s;
} ld2410_params_t;

hlk_err_t ld2410_decode_params(const hlk_ack_t *ack, ld2410_params_t *out);

// Reply of LD2410_CMD_READ_RESOLUTION: gate length in cm (75 or 20)
hlk_err_t ld2410_decode_resolution(const hlk_ack_t *ack, uint16_t *gate_cm);

// Reply of LD2410_CMD_READ_AUX_CONTROL
typedef struct {
    uint8_t light_mode;      // 0 off, 1 OUT active when light < threshold, 2 when light > threshold
    uint8_t light_threshold; // 0-255, factory 0x80
    uint8_t out_idle_level;  // OUT level without a target: 0 low (factory), 1 high
} ld2410_aux_control_t;

hlk_err_t ld2410_decode_aux_control(const hlk_ack_t *ack, ld2410_aux_control_t *out);

// Reply of LD2410_CMD_NOISE_STATUS
hlk_err_t ld2410_decode_noise_status(const hlk_ack_t *ack, uint16_t *status);

// Command values (the bytes after the command word). Each returns the value length.
// Moving and still threshold (0-100) of one gate, or of all gates with gate 0xFFFF.
size_t ld2410_value_sensitivity(uint16_t gate, uint8_t moving, uint8_t still, uint8_t out[18]);
size_t ld2410_value_max_gates(uint8_t max_moving_gate, uint8_t max_still_gate, uint16_t no_one_duration_s,
                              uint8_t out[18]);
