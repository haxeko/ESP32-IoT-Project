/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// HLK-LD2450 report and command payloads.
// Source: Hi-Link "HLK-LD2450 Serial Communication Protocol V1.03" (2023-10-17);
// see docs/phase1/giao-thuc-ld2450.md. The LD2450 reports up to three targets
// (position and speed) 10 times per second; it has no energy, distance gate or
// sensitivity data.

#include "hlk_frame.h"

#define LD2450_TARGETS 3

// LD2450-only command words
#define LD2450_CMD_SINGLE_TARGET 0x0080
#define LD2450_CMD_MULTI_TARGET 0x0090 // factory default
#define LD2450_CMD_READ_TRACKING 0x0091 // reply: 1 single, 2 multi
#define LD2450_CMD_READ_ZONES 0x00C1
#define LD2450_CMD_SET_ZONES 0x00C2 // stored, effective at once

#define LD2450_TRACKING_SINGLE 1
#define LD2450_TRACKING_MULTI 2

typedef struct {
    bool present; // the radar fills absent targets with zeros
    int16_t x_mm; // across the radar
    int16_t y_mm; // away from the radar, positive in front of it
    int16_t speed_cm_s; // sign as sent: positive = moving away (ESPHome reading, not in the Hi-Link documents)
    uint16_t resolution_mm; // "distance gate size": constant per module (0x0140 / 0x0168 in the documents)
} ld2450_target_t;

typedef struct {
    ld2450_target_t target[LD2450_TARGETS];
    uint8_t count; // present targets
} ld2450_report_t;

// Sign-magnitude value of a report: bit 15 set = positive, bits 0-14 = magnitude.
// (Not two's complement: the zone coordinates of commands 0x00C1/0x00C2 are.)
static inline int16_t ld2450_decode_signed(const uint8_t *p)
{
    const uint16_t raw = hlk_u16(p);
    const int16_t magnitude = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? magnitude : (int16_t)-magnitude;
}

// Decodes the 24 target bytes of an HLK_FRAME_LD2450_REPORT frame.
hlk_err_t ld2450_decode_report(const uint8_t *payload, size_t len, ld2450_report_t *out);

// Plausibility limits used by ld2450_target_plausible(): beyond the documented
// 6 m range and walking speeds, with margin.
#define LD2450_MAX_ABS_X_MM 8000
#define LD2450_MAX_Y_MM 8000
#define LD2450_MAX_ABS_SPEED_CM_S 800

// False for a present target with a negative Y (behind the radar) or values
// beyond the limits above: corruption the missing checksum let through, or a
// firmware that reports differently.
bool ld2450_target_plausible(const ld2450_target_t *target);

// Zone filter (commands 0x00C1 / 0x00C2): two opposite corners of up to three
// rectangles, in mm; a zone with all four values 0 is not used.
#define LD2450_ZONES 3
#define LD2450_ZONE_OFF 0     // no zone filter (factory default)
#define LD2450_ZONE_ONLY 1    // report only targets inside the zones
#define LD2450_ZONE_EXCLUDE 2 // ignore targets inside the zones

typedef struct {
    int16_t x1_mm, y1_mm, x2_mm, y2_mm;
} ld2450_zone_t;

typedef struct {
    uint16_t type; // LD2450_ZONE_*
    ld2450_zone_t zone[LD2450_ZONES];
} ld2450_zones_t;

hlk_err_t ld2450_decode_tracking(const hlk_ack_t *ack, uint16_t *mode);
hlk_err_t ld2450_decode_zones(const hlk_ack_t *ack, ld2450_zones_t *out);
// Value of LD2450_CMD_SET_ZONES; returns its length (26), 0 for an unknown type.
size_t ld2450_value_zones(const ld2450_zones_t *zones, uint8_t out[26]);
