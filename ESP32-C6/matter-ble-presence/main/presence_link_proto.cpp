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
static constexpr size_t kStatusLen = 6 + 1 + 2 * PLINK_SLOT_COUNT;
static constexpr size_t kSlotsLen = 1 + PLINK_MAC_LEN * PLINK_SLOT_COUNT;
static constexpr size_t kPairingMinLen = PLINK_MANUAL_CODE_LEN + 1; // manual code, QR length, QR code
static constexpr size_t kCommandLen = 5;
static constexpr uint8_t kCommandCheck[4] = { 'P', 'L', 'C', 'M' };

static_assert(kStatusLen <= PLINK_MAX_PAYLOAD && kSlotsLen <= PLINK_MAX_PAYLOAD &&
                  kPairingMinLen + PLINK_QR_CODE_MAX <= PLINK_MAX_PAYLOAD,
              "payload too long for PLINK_MAX_PAYLOAD");

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
                         uint64_t scan_clock_ms, uint64_t uptime_ms)
{
    uint8_t known = static_cast<uint8_t>(~active_mask) | reported_mask;
    for (int i = 0; i < slots && i < 8; i++) {
        const uint64_t timeout = timeout_ms[i];
        if (scan_clock_ms >= timeout || uptime_ms >= timeout + PLINK_UNKNOWN_GRACE_MS) {
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
    p[6] = status->bridged_mask;
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        p[7 + 2 * i] = static_cast<uint8_t>(status->endpoint[i]);
        p[8 + 2 * i] = static_cast<uint8_t>(status->endpoint[i] >> 8);
    }
    return plink_encode(PLINK_TYPE_STATUS, p, sizeof(p), out, out_size);
}

size_t plink_encode_slots(const plink_slots_t *slots, uint8_t *out, size_t out_size)
{
    uint8_t p[kSlotsLen] = {};
    p[0] = slots->active_mask & ((1u << PLINK_SLOT_COUNT) - 1);
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        if (p[0] & (1u << i)) {
            memcpy(&p[1 + PLINK_MAC_LEN * i], slots->mac[i], PLINK_MAC_LEN);
        }
    }
    return plink_encode(PLINK_TYPE_SLOTS, p, sizeof(p), out, out_size);
}

size_t plink_encode_pairing(const plink_pairing_t *pairing, uint8_t *out, size_t out_size)
{
    const size_t manual_len = strnlen(pairing->manual_code, sizeof(pairing->manual_code));
    const size_t qr_len = strnlen(pairing->qr_code, sizeof(pairing->qr_code));
    if (manual_len != PLINK_MANUAL_CODE_LEN || qr_len > PLINK_QR_CODE_MAX) {
        return 0;
    }
    uint8_t p[kPairingMinLen + PLINK_QR_CODE_MAX];
    memcpy(p, pairing->manual_code, PLINK_MANUAL_CODE_LEN);
    p[PLINK_MANUAL_CODE_LEN] = static_cast<uint8_t>(qr_len);
    memcpy(&p[kPairingMinLen], pairing->qr_code, qr_len);
    return plink_encode(PLINK_TYPE_PAIRING, p, kPairingMinLen + qr_len, out, out_size);
}

size_t plink_encode_command(const plink_command_t *command, uint8_t *out, size_t out_size)
{
    uint8_t p[kCommandLen];
    p[0] = command->command;
    memcpy(&p[1], kCommandCheck, sizeof(kCommandCheck));
    return plink_encode(PLINK_TYPE_COMMAND, p, sizeof(p), out, out_size);
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
    status->bridged_mask = payload[6] & ((1u << PLINK_SLOT_COUNT) - 1);
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        const bool bridged = status->bridged_mask & (1u << i);
        status->endpoint[i] = bridged ? static_cast<uint16_t>(payload[7 + 2 * i] | payload[8 + 2 * i] << 8) : 0;
    }
    return true;
}

bool plink_decode_slots(const uint8_t *payload, size_t len, plink_slots_t *slots)
{
    if (len != kSlotsLen || (payload[0] >> PLINK_SLOT_COUNT) != 0) {
        return false;
    }
    memset(slots, 0, sizeof(*slots));
    slots->active_mask = payload[0];
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        if (slots->active_mask & (1u << i)) {
            memcpy(slots->mac[i], &payload[1 + PLINK_MAC_LEN * i], PLINK_MAC_LEN);
        }
    }
    return true;
}

static bool is_digits(const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') {
            return false;
        }
    }
    return true;
}

// Characters of a Matter QR code payload: "MT:" then base-38 (0-9, A-Z, '-', '.')
static bool is_qr_text(const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const uint8_t c = p[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.' || c == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool plink_decode_pairing(const uint8_t *payload, size_t len, plink_pairing_t *pairing)
{
    if (len < kPairingMinLen || !is_digits(payload, PLINK_MANUAL_CODE_LEN)) {
        return false;
    }
    const size_t qr_len = payload[PLINK_MANUAL_CODE_LEN];
    if (qr_len > PLINK_QR_CODE_MAX || len != kPairingMinLen + qr_len || !is_qr_text(&payload[kPairingMinLen], qr_len)) {
        return false;
    }
    memset(pairing, 0, sizeof(*pairing));
    memcpy(pairing->manual_code, payload, PLINK_MANUAL_CODE_LEN);
    memcpy(pairing->qr_code, &payload[kPairingMinLen], qr_len);
    return true;
}

bool plink_decode_command(const uint8_t *payload, size_t len, plink_command_t *command)
{
    if (len != kCommandLen || memcmp(&payload[1], kCommandCheck, sizeof(kCommandCheck)) != 0) {
        return false;
    }
    command->command = payload[0];
    return true;
}

plink_slot_action_t plink_slot_action(bool have, const uint8_t have_mac[PLINK_MAC_LEN], bool want,
                                      const uint8_t want_mac[PLINK_MAC_LEN])
{
    if (!want) {
        return have ? PLINK_SLOT_REMOVE : PLINK_SLOT_KEEP;
    }
    if (!have) {
        return PLINK_SLOT_ADD;
    }
    return memcmp(have_mac, want_mac, PLINK_MAC_LEN) == 0 ? PLINK_SLOT_KEEP : PLINK_SLOT_REPLACE;
}

void plink_mac_string(const uint8_t mac[PLINK_MAC_LEN], char out[PLINK_MAC_STR_SIZE])
{
    static const char kHex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (int i = 0; i < PLINK_MAC_LEN; i++) {
        if (i > 0) {
            out[o++] = ':';
        }
        out[o++] = kHex[mac[i] >> 4];
        out[o++] = kHex[mac[i] & 0x0F];
    }
    out[o] = '\0';
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
