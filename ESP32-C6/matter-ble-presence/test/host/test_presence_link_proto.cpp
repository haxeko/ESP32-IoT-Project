// UART link frames of the two-chip version (main/presence_link_proto.cpp): round trip, every corrupted byte and
// every truncation rejected, resynchronisation after garbage such as ROM boot messages.
#include "test.h"

#include <stdlib.h>

#include "presence_link_proto.h"

static int feed_all(plink_parser_t *p, const uint8_t *data, size_t len, uint8_t *last_type)
{
    int frames = 0;
    for (size_t i = 0; i < len; i++) {
        if (plink_parser_feed(p, data[i])) {
            frames++;
            *last_type = p->type;
        }
    }
    return frames;
}

static void state_round_trip()
{
    const plink_state_t in = { PLINK_MODE_NORMAL, PLINK_STATE_SCANNING, 0x1F, 0x05, 123456 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_state(&in, frame, sizeof(frame));
    CHECK(n == 17);
    plink_parser_t p;
    plink_parser_init(&p);
    uint8_t type = 0;
    CHECK(feed_all(&p, frame, n, &type) == 1);
    CHECK(type == PLINK_TYPE_STATE);
    plink_state_t out;
    CHECK(plink_decode_state(p.payload, p.len, &out));
    CHECK(out.mode == in.mode && out.flags == in.flags && out.active_mask == 0x1F && out.present_mask == 0x05);
    CHECK(out.uptime_s == 123456);
    plink_status_t wrong;
    CHECK(!plink_decode_status(p.payload, p.len, &wrong)); // a state frame is not a status
}

static void status_round_trip()
{
    const plink_status_t in = { PLINK_STATUS_MATTER_STARTED | PLINK_STATUS_COMMISSIONED, 2, 42 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_status(&in, frame, sizeof(frame));
    plink_parser_t p;
    plink_parser_init(&p);
    uint8_t type = 0;
    CHECK(feed_all(&p, frame, n, &type) == 1 && type == PLINK_TYPE_STATUS);
    plink_status_t out;
    CHECK(plink_decode_status(p.payload, p.len, &out));
    CHECK(out.flags == in.flags && out.fabrics == 2 && out.uptime_s == 42);
}

static void present_only_for_active_slots()
{
    const plink_state_t in = { PLINK_MODE_NORMAL, 0, 0x03, 0x1F, 0 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_state(&in, frame, sizeof(frame));
    plink_parser_t p;
    plink_parser_init(&p);
    uint8_t type = 0;
    CHECK(feed_all(&p, frame, n, &type) == 1);
    plink_state_t out;
    CHECK(plink_decode_state(p.payload, p.len, &out) && out.present_mask == 0x03);
}

static void every_corruption_rejected()
{
    const plink_state_t in = { PLINK_MODE_NORMAL, 1, 0x1F, 0x0A, 99 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_state(&in, frame, sizeof(frame));
    for (size_t i = 0; i < n; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t bad[PLINK_MAX_FRAME];
            memcpy(bad, frame, n);
            bad[i] ^= static_cast<uint8_t>(1u << bit);
            plink_parser_t p;
            plink_parser_init(&p);
            uint8_t type = 0;
            CHECK(feed_all(&p, bad, n, &type) == 0);
            // A damaged length byte can make the parser swallow the next frame; the one after it is received
            int got = feed_all(&p, frame, n, &type);
            got += feed_all(&p, frame, n, &type);
            CHECK(got >= 1);
        }
    }
}

static void truncation_then_resync()
{
    const plink_state_t in = { PLINK_MODE_CONFIG, 0, 0x01, 0, 7 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_state(&in, frame, sizeof(frame));
    for (size_t cut = 1; cut < n; cut++) {
        plink_parser_t p;
        plink_parser_init(&p);
        uint8_t type = 0;
        CHECK(feed_all(&p, frame, cut, &type) == 0);
        // a cut frame followed by full frames: at most the first full one is swallowed by the cut one
        int got = feed_all(&p, frame, n, &type);
        got += feed_all(&p, frame, n, &type);
        got += feed_all(&p, frame, n, &type);
        CHECK(got >= 2);
    }
}

static void garbage_like_rom_messages()
{
    const char *rom = "ESP-ROM:esp32c3-api1-20210207\r\nBuild:Feb  7 2021\r\nrst:0x1 (POWERON),boot:0xc (SPI_FAST_FLASH_BOOT)\r\n"
                      "\xA5\xA5\x5A\x01\x01\xFF garbage \xA5\x5A\x07";
    const plink_status_t in = { PLINK_STATUS_NETWORK_UP, 1, 5 };
    uint8_t frame[PLINK_MAX_FRAME];
    const size_t n = plink_encode_status(&in, frame, sizeof(frame));
    plink_parser_t p;
    plink_parser_init(&p);
    uint8_t type = 0;
    CHECK(feed_all(&p, reinterpret_cast<const uint8_t *>(rom), strlen(rom), &type) == 0);
    int got = 0;
    for (int i = 0; i < 3; i++) {
        got += feed_all(&p, frame, n, &type);
    }
    CHECK(got >= 2);

    // random noise never yields a frame with a bad CRC accepted as good
    srand(12345);
    plink_parser_init(&p);
    int accepted = 0;
    for (int i = 0; i < 200000; i++) {
        if (plink_parser_feed(&p, static_cast<uint8_t>(rand()))) {
            accepted++;
        }
    }
    CHECK(accepted == 0);
}

static void encode_limits()
{
    uint8_t payload[PLINK_MAX_PAYLOAD + 1] = {};
    uint8_t out[PLINK_MAX_FRAME + 8];
    CHECK(plink_encode(PLINK_TYPE_STATE, payload, PLINK_MAX_PAYLOAD + 1, out, sizeof(out)) == 0);
    CHECK(plink_encode(PLINK_TYPE_STATE, payload, PLINK_MAX_PAYLOAD, out, sizeof(out)) == PLINK_MAX_FRAME);
    CHECK(plink_encode(PLINK_TYPE_STATE, payload, 4, out, 8) == 0);
}

int main()
{
    RUN(state_round_trip);
    RUN(status_round_trip);
    RUN(present_only_for_active_slots);
    RUN(every_corruption_rejected);
    RUN(truncation_then_resync);
    RUN(garbage_like_rom_messages);
    RUN(encode_limits);
    return test_summary("presence_link");
}
