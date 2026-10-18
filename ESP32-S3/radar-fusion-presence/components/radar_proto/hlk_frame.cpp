/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hlk_frame.h"

static const uint8_t k_ld2410_header[] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t k_ld2410_footer[] = { 0xF8, 0xF7, 0xF6, 0xF5 };
static const uint8_t k_ld2450_header[] = { 0xAA, 0xFF, 0x03, 0x00 };
static const uint8_t k_ld2450_footer[] = { 0x55, 0xCC };
static const uint8_t k_cmd_header[] = { 0xFD, 0xFC, 0xFB, 0xFA };
static const uint8_t k_cmd_footer[] = { 0x04, 0x03, 0x02, 0x01 };

typedef struct {
    const uint8_t *header;
    const uint8_t *footer;
    size_t footer_len;
    bool has_length; // LEN field after the header; otherwise an LD2450 report of fixed size
    hlk_frame_kind_t kind;
} frame_format_t;

// The first header bytes differ, so a byte starts at most one format.
static const frame_format_t k_formats[] = {
    { k_ld2410_header, k_ld2410_footer, sizeof(k_ld2410_footer), true, HLK_FRAME_LD2410_REPORT },
    { k_ld2450_header, k_ld2450_footer, sizeof(k_ld2450_footer), false, HLK_FRAME_LD2450_REPORT },
    { k_cmd_header, k_cmd_footer, sizeof(k_cmd_footer), true, HLK_FRAME_ACK },
};

// Format whose header starts with the first len bytes of buf (as far as they go)
static const frame_format_t *match_header(const uint8_t *buf, size_t len)
{
    const size_t n = len < HLK_HEADER_LEN ? len : HLK_HEADER_LEN;
    for (const frame_format_t &f : k_formats) {
        if (memcmp(buf, f.header, n) == 0) {
            return &f;
        }
    }
    return nullptr;
}

static bool starts_frame(uint8_t b)
{
    return b == k_ld2410_header[0] || b == k_ld2450_header[0] || b == k_cmd_header[0];
}

void hlk_parser_init(hlk_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

static void drop_first_byte(hlk_parser_t *p)
{
    memmove(p->buf, p->buf + 1, p->len - 1);
    p->len--;
    p->stats.garbage_bytes++;
}

// Hands every complete frame at the start of the buffer to cb; returns when
// the buffer is empty or the next frame needs more bytes.
static void extract_frames(hlk_parser_t *p, hlk_frame_cb_t cb, void *ctx, size_t input_offset)
{
    while (p->len > 0) {
        const frame_format_t *f = match_header(p->buf, p->len);
        if (!f) {
            drop_first_byte(p);
            continue;
        }
        if (p->len < HLK_HEADER_LEN) {
            return;
        }
        size_t payload_len = HLK_LD2450_PAYLOAD_LEN;
        size_t payload_pos = HLK_HEADER_LEN;
        if (f->has_length) {
            if (p->len < HLK_HEADER_LEN + 2) {
                return;
            }
            payload_len = hlk_u16(&p->buf[HLK_HEADER_LEN]);
            payload_pos = HLK_HEADER_LEN + 2;
            if (payload_len > HLK_MAX_PAYLOAD) {
                p->stats.bad_length++;
                drop_first_byte(p);
                continue;
            }
        }
        const size_t frame_len = payload_pos + payload_len + f->footer_len;
        if (p->len < frame_len) {
            return;
        }
        if (memcmp(&p->buf[frame_len - f->footer_len], f->footer, f->footer_len) != 0) {
            p->stats.bad_footer++;
            drop_first_byte(p);
            continue;
        }

        hlk_frame_kind_t kind = f->kind;
        if (kind == HLK_FRAME_ACK && (payload_len < 2 || (hlk_u16(&p->buf[payload_pos]) & HLK_ACK_FLAG) == 0)) {
            kind = HLK_FRAME_COMMAND;
        }
        p->stats.frames++;
        if (cb) {
            const hlk_frame_t frame = {
                .kind = kind,
                .frame = p->buf,
                .frame_len = frame_len,
                .payload = &p->buf[payload_pos],
                .payload_len = payload_len,
                .input_offset = input_offset,
            };
            cb(&frame, ctx);
        }
        memmove(p->buf, p->buf + frame_len, p->len - frame_len);
        p->len -= frame_len;
    }
}

void hlk_parser_feed(hlk_parser_t *parser, const uint8_t *data, size_t len, hlk_frame_cb_t cb, void *ctx)
{
    for (size_t i = 0; i < len; i++) {
        if (parser->len == 0 && !starts_frame(data[i])) {
            parser->stats.garbage_bytes++;
            continue;
        }
        // extract_frames() always leaves less than a full frame, so this cannot overflow
        if (parser->len >= sizeof(parser->buf)) {
            drop_first_byte(parser);
        }
        parser->buf[parser->len++] = data[i];
        extract_frames(parser, cb, ctx, i + 1);
    }
}

// ---- Commands and ACKs ----

static const uint32_t k_baud_rates[] = { 9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800 };

uint16_t hlk_baud_index(uint32_t baud)
{
    for (size_t i = 0; i < sizeof(k_baud_rates) / sizeof(k_baud_rates[0]); i++) {
        if (k_baud_rates[i] == baud) {
            return (uint16_t)(i + 1);
        }
    }
    return 0;
}

uint32_t hlk_baud_rate(uint16_t index)
{
    return index >= 1 && index <= sizeof(k_baud_rates) / sizeof(k_baud_rates[0]) ? k_baud_rates[index - 1] : 0;
}

size_t hlk_build_command(uint16_t command, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap)
{
    const size_t data_len = 2 + value_len;
    const size_t frame_len = sizeof(k_cmd_header) + 2 + data_len + sizeof(k_cmd_footer);
    if (value_len > HLK_MAX_PAYLOAD - 2 || frame_len > cap || (value_len > 0 && !value)) {
        return 0;
    }
    size_t n = 0;
    memcpy(&out[n], k_cmd_header, sizeof(k_cmd_header));
    n += sizeof(k_cmd_header);
    hlk_put_u16(&out[n], (uint16_t)data_len);
    n += 2;
    hlk_put_u16(&out[n], command);
    n += 2;
    if (value_len > 0) {
        memcpy(&out[n], value, value_len);
        n += value_len;
    }
    memcpy(&out[n], k_cmd_footer, sizeof(k_cmd_footer));
    n += sizeof(k_cmd_footer);
    return n;
}

const char *hlk_err_name(hlk_err_t err)
{
    switch (err) {
    case HLK_OK:
        return "ok";
    case HLK_ERR_SHORT:
        return "short";
    case HLK_ERR_TYPE:
        return "type";
    case HLK_ERR_MARKER:
        return "marker";
    case HLK_ERR_GATES:
        return "gates";
    case HLK_ERR_VALUE:
        return "value";
    }
    return "?";
}

hlk_err_t hlk_decode_ack(const uint8_t *payload, size_t len, hlk_ack_t *out)
{
    if (len < 4) {
        return HLK_ERR_SHORT;
    }
    const uint16_t word = hlk_u16(payload);
    if ((word & HLK_ACK_FLAG) == 0) {
        return HLK_ERR_TYPE;
    }
    out->command = (uint16_t)(word & ~HLK_ACK_FLAG);
    out->status = hlk_u16(&payload[2]);
    out->value = &payload[4];
    out->value_len = len - 4;
    return HLK_OK;
}

hlk_err_t hlk_decode_version(const hlk_ack_t *ack, hlk_version_t *out)
{
    if (ack->value_len < 8) {
        return HLK_ERR_SHORT;
    }
    const uint8_t *v = ack->value;
    out->type = hlk_u16(v);
    out->major = hlk_u16(&v[2]);
    out->minor = (uint32_t)v[4] | ((uint32_t)v[5] << 8) | ((uint32_t)v[6] << 16) | ((uint32_t)v[7] << 24);
    return HLK_OK;
}

void hlk_format_version(const hlk_version_t *version, char *buf, size_t len)
{
    snprintf(buf, len, "V%u.%02X.%08" PRIX32, (unsigned)(version->major >> 8), (unsigned)(version->major & 0xFF),
             version->minor);
}

hlk_err_t hlk_decode_enable_config(const hlk_ack_t *ack, hlk_config_info_t *out)
{
    if (ack->value_len < 4) {
        return HLK_ERR_SHORT;
    }
    out->protocol_version = hlk_u16(ack->value);
    out->buffer_size = hlk_u16(&ack->value[2]);
    return HLK_OK;
}

hlk_err_t hlk_decode_mac(const hlk_ack_t *ack, uint8_t mac[HLK_MAC_LEN], bool *bluetooth_on)
{
    static const uint8_t k_bt_off[HLK_MAC_LEN] = { 0x08, 0x05, 0x04, 0x03, 0x02, 0x01 };
    static const uint8_t k_zero[HLK_MAC_LEN] = {};
    static const uint8_t k_ff[HLK_MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (ack->value_len < HLK_MAC_LEN) {
        return HLK_ERR_SHORT;
    }
    memcpy(mac, ack->value, HLK_MAC_LEN);
    *bluetooth_on = memcmp(mac, k_bt_off, HLK_MAC_LEN) != 0 && memcmp(mac, k_zero, HLK_MAC_LEN) != 0 &&
                    memcmp(mac, k_ff, HLK_MAC_LEN) != 0;
    return HLK_OK;
}
