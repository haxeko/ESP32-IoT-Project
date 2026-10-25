/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Frames of the UART link of the two-chip version (ESP32-C3 scanner <-> ESP32-H2 Matter node). No ESP-IDF
// dependency (host tests).
//
//   0xA5 0x5A | version | type | length | payload (length bytes) | CRC-32 of version..payload (little endian)
//
// The line also carries garbage: each chip's ROM prints its boot messages on its UART0 TX pin, and the link can use
// those pins. The parser hunts for the two magic bytes, checks the length and the CRC, and starts hunting again
// after any error, so garbage only costs counted errors (a damaged length byte can also swallow the next frame).
// Frames repeat every second, so none needs to be resent.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLINK_VERSION 2 // 2: known_mask added to STATE; both chips need firmware of the same version
#define PLINK_MAX_PAYLOAD 16
#define PLINK_MAX_FRAME (2 + 3 + PLINK_MAX_PAYLOAD + 4)

typedef enum {
    PLINK_TYPE_STATE = 1,  // scanner -> Matter node
    PLINK_TYPE_STATUS = 2, // Matter node -> scanner
} plink_type_t;

#define PLINK_MODE_NORMAL 0 // presence computed: present_mask is valid for the slots of known_mask
#define PLINK_MODE_CONFIG 1 // web configuration running: no presence, the Matter node keeps its states

#define PLINK_STATE_SCANNING 0x01 // the radio is scanning (not paused, not in backoff)

// Sent by the scanner every second and right after a change
typedef struct {
    uint8_t mode;         // PLINK_MODE_*
    uint8_t flags;        // PLINK_STATE_*
    uint8_t active_mask;  // bit n: slot n has a beacon assigned and is enabled
    uint8_t present_mask; // bit n: slot n present (only bits of active slots can be set)
    uint8_t known_mask;   // bit n: slot n's state is known; the Matter node keeps the state of the others (the bits of
                          // inactive slots always read as known: they are off)
    uint32_t uptime_s;
} plink_state_t;

#define PLINK_STATUS_MATTER_STARTED 0x01
#define PLINK_STATUS_COMMISSIONED 0x02
#define PLINK_STATUS_NETWORK_UP 0x04 // Thread attached

// Sent by the Matter node every second
typedef struct {
    uint8_t flags; // PLINK_STATUS_*
    uint8_t fabrics;
    uint32_t uptime_s;
} plink_status_t;

// Scanner: the known_mask of its STATE frames. A slot's state is known once it was reported (it turned on, or off
// after being on: reported_mask) or once the scanner listened for the slot's whole absence timeout (scan clock, ms)
// without hearing its beacon; inactive slots are known (off). Until then the Matter node keeps the state it has, so
// a restart of the scanner (end of the CONFIG mode, a reset) never turns a sensor off by itself. A scanner that
// cannot scan at all (BLE failure: the scan clock stands still) still declares the slot known, as absent, once its
// uptime passes the timeout by PLINK_UNKNOWN_GRACE_MS: a stale "occupied" does not last forever.
#define PLINK_UNKNOWN_GRACE_MS (5u * 60u * 1000u)
uint8_t plink_known_mask(uint8_t active_mask, uint8_t reported_mask, const uint32_t *timeout_ms, int slots,
                         uint64_t scan_clock_ms, uint64_t uptime_ms);

// Frame into out; returns its length, 0 if it does not fit or the payload is too long
size_t plink_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t out_size);
size_t plink_encode_state(const plink_state_t *state, uint8_t *out, size_t out_size);
size_t plink_encode_status(const plink_status_t *status, uint8_t *out, size_t out_size);

// Payload of a received frame; false if its length does not match the type
bool plink_decode_state(const uint8_t *payload, size_t len, plink_state_t *state);
bool plink_decode_status(const uint8_t *payload, size_t len, plink_status_t *status);

typedef struct {
    uint8_t buf[PLINK_MAX_FRAME];
    size_t have; // bytes of the current frame received so far (magic included)
    uint8_t type;
    uint8_t len;
    const uint8_t *payload; // valid after plink_parser_feed() returned true, until the next byte
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t bad_frames; // unknown version, length too long
} plink_parser_t;

void plink_parser_init(plink_parser_t *p);
// One received byte; true when it completes a valid frame (type, len, payload set)
bool plink_parser_feed(plink_parser_t *p, uint8_t byte);
