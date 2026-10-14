/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// Byte-by-byte parser of the HLK-LD2410 target report frames. Plain C without
// ESP-IDF dependencies, so it also runs in the host tests (test/host).
//
// Frame: F4 F3 F2 F1 | LEN (2, LE) | TYPE AA STATE MOV_CM(2) MOV_E STILL_CM(2) STILL_E DET_CM(2) ... 55 00 | F8 F7 F6 F5
// TYPE is 0x02 in the default (basic) reporting mode, 0x01 in engineering mode.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Longest payload accepted (basic report: 13 bytes, engineering report: 35).
#define LD2410_MAX_PAYLOAD 64

typedef struct {
    bool moving;            // a moving target is detected
    bool still;             // a still (stationary) target is detected
    uint16_t moving_cm;     // distance of the moving target
    uint16_t still_cm;      // distance of the still target
    uint16_t detection_cm;  // "detection distance" reported by the radar
} ld2410_report_t;

typedef struct {
    uint8_t buf[4 + 2 + LD2410_MAX_PAYLOAD + 4];
    size_t pos;
    size_t frame_len;
} ld2410_parser_t;

void ld2410_parser_init(ld2410_parser_t *parser);

// Feeds one received byte. Returns true when the byte completed a valid
// target report frame, which is then decoded into *report. Other frames
// (command ACKs, corrupted or oversized frames) are skipped.
bool ld2410_parser_feed(ld2410_parser_t *parser, uint8_t byte, ld2410_report_t *report);

#ifdef __cplusplus
}
#endif
