/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "presence_link_proto.h"

#include <string.h>

#include "crc32.h"

static constexpr uint8_t kMagic0 = 0xA5;
static constexpr uint8_t kMagic1 = 0x5A;
static constexpr size_t kHeader = 5; // magic, magic, version, type, length
static constexpr size_t kStateLen = 9;
static constexpr size_t kStatusLen = 6;

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

uint8_t plink_known_mask(uint8_t active_mask, uint8_t reported_mask, const uint32_t *timeout_ms, int slots,
                         uint64_t scan_clock_ms)
{
    uint8_t known = static_cast<uint8_t>(~active_mask) | reported_mask;
    for (int i = 0; i < slots && i < 8; i++) {
        if (scan_clock_ms >= timeout_ms[i]) {
            known |= static_cast<uint8_t>(1u << i);
        }
    }
    return known;
}

size_t plink_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t out_size)
{
    const size_t total = kHeader + len + 4;
    if (len > PLINK_MAX_PAYLOAD || total > out_size) {
        return 0;
    }
    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = PLINK_VERSION;
    out[3] = type;
    out[4] = static_cast<uint8_t>(len);
    if (len > 0) {
        memcpy(&out[kHeader], payload, len);
    }
    put_u32(&out[kHeader + len], crc32_ieee(&out[2], 3 + len));
    return total;
}

size_t plink_encode_state(const plink_state_t *state, uint8_t *out, size_t out_size)
{
    uint8_t p[kStateLen];
    p[0] = state->mode;
    p[1] = state->flags;
    p[2] = state->active_mask;
    p[3] = state->present_mask & state->active_mask;
    p[4] = state->known_mask;
    put_u32(&p[5], state->uptime_s);
    return plink_encode(PLINK_TYPE_STATE, p, sizeof(p), out, out_size);
}

size_t plink_encode_status(const plink_status_t *status, uint8_t *out, size_t out_size)
{
    uint8_t p[kStatusLen];
    p[0] = status->flags;
    p[1] = status->fabrics;
    put_u32(&p[2], status->uptime_s);
    return plink_encode(PLINK_TYPE_STATUS, p, sizeof(p), out, out_size);
}

bool plink_decode_state(const uint8_t *payload, size_t len, plink_state_t *state)
{
    if (len != kStateLen) {
        return false;
    }
    state->mode = payload[0];
    state->flags = payload[1];
    state->active_mask = payload[2];
    state->present_mask = payload[3] & payload[2];
    state->known_mask = payload[4] | static_cast<uint8_t>(~payload[2]);
    state->uptime_s = get_u32(&payload[5]);
    return true;
}

bool plink_decode_status(const uint8_t *payload, size_t len, plink_status_t *status)
{
    if (len != kStatusLen) {
        return false;
    }
    status->flags = payload[0];
    status->fabrics = payload[1];
    status->uptime_s = get_u32(&payload[2]);
    return true;
}

void plink_parser_init(plink_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

bool plink_parser_feed(plink_parser_t *p, uint8_t byte)
{
    if (p->have == 0) {
        if (byte == kMagic0) {
            p->buf[p->have++] = byte;
        }
        return false;
    }
    if (p->have == 1) {
        if (byte == kMagic1) {
            p->buf[p->have++] = byte;
        } else {
            p->have = byte == kMagic0 ? 1 : 0; // A5 A5 5A: the second A5 may start the frame
        }
        return false;
    }
    p->buf[p->have++] = byte;
    if (p->have == kHeader) {
        if (p->buf[2] != PLINK_VERSION || p->buf[4] > PLINK_MAX_PAYLOAD) {
            p->bad_frames++;
            p->have = 0;
        }
        return false;
    }
    if (p->have < kHeader) {
        return false;
    }
    const size_t len = p->buf[4];
    if (p->have < kHeader + len + 4) {
        return false;
    }
    p->have = 0;
    if (get_u32(&p->buf[kHeader + len]) != crc32_ieee(&p->buf[2], 3 + len)) {
        p->crc_errors++;
        return false;
    }
    p->type = p->buf[3];
    p->len = static_cast<uint8_t>(len);
    p->payload = &p->buf[kHeader];
    p->frames++;
    return true;
}
