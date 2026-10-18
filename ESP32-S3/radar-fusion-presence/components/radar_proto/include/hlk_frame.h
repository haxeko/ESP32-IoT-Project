/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// Byte stream framing of the Hi-Link HLK-LD2410 (B/C) and HLK-LD2450 24 GHz
// radars, and the parts their serial protocols share: command frames, ACK
// frames, firmware version and MAC replies. Plain C/C++ without ESP-IDF, so the
// host tests (test/host) build it with gcc.
//
// Frames on the radar's TX line (little endian):
//   LD2410 report  F4 F3 F2 F1 | LEN (2) | payload (LEN bytes)          | F8 F7 F6 F5
//   LD2450 report  AA FF 03 00 |           3 targets x 8 bytes (fixed) | 55 CC
//   ACK (both)     FD FC FB FA | LEN (2) | COMMAND|0x0100 (2) STATUS (2) value | 04 03 02 01
// Commands sent to the radar use the ACK layout without STATUS.
//
// Neither protocol has a checksum: a frame is accepted on its header, its length
// (LD2410 report, ACK) and its footer; the decoders in ld2410_proto.h and
// ld2450_proto.h then check the markers and value ranges inside.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Longest LEN accepted. The documented frames carry at most 35 bytes (LD2410
// engineering report); a larger LEN is taken as corruption.
#define HLK_MAX_PAYLOAD 64
#define HLK_HEADER_LEN 4
#define HLK_FOOTER_LEN 4
#define HLK_LD2450_FRAME_LEN 30
#define HLK_LD2450_PAYLOAD_LEN 24
#define HLK_MAX_FRAME (HLK_HEADER_LEN + 2 + HLK_MAX_PAYLOAD + HLK_FOOTER_LEN)

typedef enum {
    HLK_FRAME_LD2410_REPORT = 1,
    HLK_FRAME_LD2450_REPORT,
    HLK_FRAME_ACK,
    HLK_FRAME_COMMAND, // a command frame without the ACK flag: our own TX looped back (wiring fault)
} hlk_frame_kind_t;

typedef struct {
    hlk_frame_kind_t kind;
    const uint8_t *frame;  // the whole frame, header to footer
    size_t frame_len;
    const uint8_t *payload; // LD2410 report / ACK: the LEN bytes; LD2450 report: the 24 target bytes
    size_t payload_len;
    size_t input_offset; // bytes of the current hlk_parser_feed() input used when the frame completed
} hlk_frame_t;

// Called for every complete frame. The pointers are only valid during the call.
typedef void (*hlk_frame_cb_t)(const hlk_frame_t *frame, void *ctx);

typedef struct {
    uint32_t frames;        // complete frames (header, length and footer valid)
    uint32_t garbage_bytes; // bytes dropped while looking for a header, including the first byte of each bad frame
    uint32_t bad_length;    // LEN above HLK_MAX_PAYLOAD (LD2410 report, ACK)
    uint32_t bad_footer;    // a frame whose footer did not match
} hlk_parser_stats_t;

typedef struct {
    uint8_t buf[HLK_MAX_FRAME];
    size_t len;
    hlk_parser_stats_t stats;
} hlk_parser_t;

void hlk_parser_init(hlk_parser_t *parser);

// Splits the stream into frames and calls cb for each one, in order. Accepts
// LD2410 reports, LD2450 reports and ACKs on any line, so the frame kind also
// tells which radar is wired to a UART. Never blocks and never allocates; a
// frame split over several calls is kept in the parser. After a bad frame the
// bytes after its first byte are searched again, so a valid frame that started
// inside a corrupted one is not lost.
void hlk_parser_feed(hlk_parser_t *parser, const uint8_t *data, size_t len, hlk_frame_cb_t cb, void *ctx);

// ---- Commands and ACKs ----

// Command words shared by the LD2410 (B/C) and the LD2450
#define HLK_CMD_ENABLE_CONFIG 0x00FF // value 0x0001; reply: protocol version (2), buffer size (2)
#define HLK_CMD_END_CONFIG 0x00FE
#define HLK_CMD_READ_VERSION 0x00A0 // reply: firmware type (2), major (2), minor (4)
#define HLK_CMD_SET_BAUD 0x00A1     // value: baud index (2), effective after a restart
#define HLK_CMD_FACTORY_RESET 0x00A2
#define HLK_CMD_RESTART 0x00A3
#define HLK_CMD_BLUETOOTH 0x00A4 // value 0x0001 on, 0x0000 off, effective after a restart
#define HLK_CMD_READ_MAC 0x00A5  // value 0x0001; reply: MAC (6)
#define HLK_ACK_FLAG 0x0100

// Baud rate index of HLK_CMD_SET_BAUD (1-8); 0 for a rate the radar does not offer.
uint16_t hlk_baud_index(uint32_t baud);
// Baud rate of an index, 0 when unknown.
uint32_t hlk_baud_rate(uint16_t index);

// Writes a command frame into out; returns its length, 0 when cap is too small
// or the value too long.
size_t hlk_build_command(uint16_t command, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap);

typedef enum {
    HLK_OK = 0,
    HLK_ERR_SHORT,  // payload or value too short
    HLK_ERR_TYPE,   // unknown report type / not an ACK
    HLK_ERR_MARKER, // LD2410 report: 0xAA head, 0x55 tail or 0x00 check byte missing; reply header byte wrong
    HLK_ERR_GATES,  // LD2410: gate count above 9, or energies longer than the frame
    HLK_ERR_VALUE,  // a value outside its documented range
} hlk_err_t;

const char *hlk_err_name(hlk_err_t err);

typedef struct {
    uint16_t command; // command word, without HLK_ACK_FLAG
    uint16_t status;  // 0 success, 1 failure
    const uint8_t *value; // return value after the status (points into the frame)
    size_t value_len;
} hlk_ack_t;

hlk_err_t hlk_decode_ack(const uint8_t *payload, size_t len, hlk_ack_t *out);

typedef struct {
    uint16_t type;  // 0x0001 (LD2410 docs), 0x0000 (LD2450 docs)
    uint16_t major; // 0x0244 is shown as 2.44
    uint32_t minor; // 0x24073110 is shown as 24073110
} hlk_version_t;

hlk_err_t hlk_decode_version(const hlk_ack_t *ack, hlk_version_t *out);
// "V2.44.24073110"
void hlk_format_version(const hlk_version_t *version, char *buf, size_t len);

typedef struct {
    uint16_t protocol_version;
    uint16_t buffer_size;
} hlk_config_info_t;

hlk_err_t hlk_decode_enable_config(const hlk_ack_t *ack, hlk_config_info_t *out);

#define HLK_MAC_LEN 6
// bluetooth_on is false when the radar answers the placeholder 08:05:04:03:02:01
// it sends while its Bluetooth is off (observed by ESPHome, not in the Hi-Link
// documents), or an all-zero / all-0xFF address.
hlk_err_t hlk_decode_mac(const hlk_ack_t *ack, uint8_t mac[HLK_MAC_LEN], bool *bluetooth_on);

// Little-endian helpers
static inline uint16_t hlk_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline void hlk_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
