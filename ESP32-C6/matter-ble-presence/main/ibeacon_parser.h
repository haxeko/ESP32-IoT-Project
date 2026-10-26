/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// iBeacon and Eddystone-TLM frames in BLE advertising data. No ESP-IDF dependency (host tests).
//
// Advertising data is a list of AD structures: [length][AD type][length - 1 bytes]. An iBeacon is the AD type 0xFF
// (Manufacturer Specific Data) structure of length 0x1A whose 25 bytes are
//   4C 00            company identifier 0x004C (Apple), little endian
//   02 15            iBeacon type, 21 bytes follow
//   16 bytes         proximity UUID
//   2 bytes          major, big endian
//   2 bytes          minor, big endian
//   1 byte           measured power: RSSI at 1 m, signed dBm
// usually after a Flags structure (02 01 06). Every length is checked against the received length before the
// bytes are read; a structure that runs past the end stops the parse.
//
// Eddystone-TLM (telemetry; many beacons send it between their iBeacon frames, from the same address) is the AD
// type 0x16 (Service Data, 16-bit UUID) structure of length 0x11 whose 16 bytes are
//   AA FE            Eddystone service UUID 0xFEAA, little endian
//   20               frame type TLM
//   00               version: unencrypted
//   2 bytes          battery voltage, mV, big endian (0: not measured)
//   2 bytes          temperature, signed 8.8 fixed point degrees Celsius, big endian (0x8000: not measured)
//   4 bytes          advertising PDUs sent since power-up, big endian
//   4 bytes          time since power-up, 0.1 s, big endian
// A TLM frame carries no identifier: it belongs to the beacon whose address sent it.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IBEACON_UUID_LEN 16
#define IBEACON_UUID_STR_SIZE 37 // 8-4-4-4-12 hex digits and the NUL

typedef struct {
    uint8_t uuid[IBEACON_UUID_LEN];
    uint16_t major;
    uint16_t minor;
    int8_t tx_power; // measured power at 1 m, dBm
} ibeacon_t;

// True and *out filled when data (one advertising PDU or scan response payload, at most 31 bytes for legacy
// advertising) carries a well-formed iBeacon structure. out is untouched otherwise.
bool ibeacon_parse(const uint8_t *data, size_t len, ibeacon_t *out);

#define EDDYSTONE_TLM_TEMP_UNKNOWN INT16_MIN // 0x8000

typedef struct {
    uint16_t battery_mv;    // 0: not measured
    int16_t temperature_q8; // 1/256 degree Celsius, EDDYSTONE_TLM_TEMP_UNKNOWN if not measured
    uint32_t adv_count;
    uint32_t uptime_ds; // 0.1 s
} eddystone_tlm_t;

// True and *out filled when data carries an unencrypted Eddystone-TLM frame. out is untouched otherwise.
bool eddystone_tlm_parse(const uint8_t *data, size_t len, eddystone_tlm_t *out);

// Complete Local Name (0x09), else Shortened Local Name (0x08), as sanitized UTF-8 (see utf8_sanitize()).
// Returns its length, 0 (and out = "") if there is none.
size_t adv_parse_name(const uint8_t *data, size_t len, char *out, size_t out_size);

// "E2C56DB5-DFFB-48D2-B060-D0F5A71096E0"
void ibeacon_uuid_format(const uint8_t uuid[IBEACON_UUID_LEN], char out[IBEACON_UUID_STR_SIZE]);
