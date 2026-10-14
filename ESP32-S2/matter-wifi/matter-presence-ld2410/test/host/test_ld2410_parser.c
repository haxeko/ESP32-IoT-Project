// Host tests of the LD2410 frame parser (main/drivers/ld2410_parser.c)
#include <string.h>

#include "check.h"
#include "ld2410_parser.h"

// Basic report: moving target 120 cm, still target 100 cm, detection distance 130 cm.
static const uint8_t k_basic[] = {
    0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00,                         // header, length 13
    0x02, 0xAA, 0x03, 0x78, 0x00, 0x50, 0x64, 0x00, 0x40, 0x82, 0x00, 0x55, 0x00,
    0xF8, 0xF7, 0xF6, 0xF5,                                     // footer
};

// Engineering report (type 0x01, 35-byte payload): no target.
static uint8_t k_engineering[4 + 2 + 35 + 4];

// Reply to a command (FD FC FB FA ... 04 03 02 01): never a target report.
static const uint8_t k_ack[] = { 0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01 };

static int feed(ld2410_parser_t *p, const uint8_t *data, size_t len, ld2410_report_t *last)
{
    int reports = 0;
    for (size_t i = 0; i < len; i++) {
        ld2410_report_t r;
        if (ld2410_parser_feed(p, data[i], &r)) {
            *last = r;
            reports++;
        }
    }
    return reports;
}

static void build_engineering(void)
{
    uint8_t *f = k_engineering;
    memcpy(f, "\xF4\xF3\xF2\xF1", 4);
    f[4] = 35;
    f[5] = 0;
    memset(&f[6], 0x11, 35);
    f[6] = 0x01;  // engineering mode
    f[7] = 0xAA;
    f[8] = 0x00;  // no target
    memcpy(&f[6 + 35], "\xF8\xF7\xF6\xF5", 4);
}

static void test_basic_frame(void)
{
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r = { 0 };
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
    CHECK(r.moving);
    CHECK(r.still);
    CHECK_EQ(r.moving_cm, 120);
    CHECK_EQ(r.still_cm, 100);
    CHECK_EQ(r.detection_cm, 130);
}

static void test_report_only_on_last_byte(void)
{
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    for (size_t i = 0; i + 1 < sizeof(k_basic); i++) {
        CHECK(!ld2410_parser_feed(&p, k_basic[i], &r));
    }
    CHECK(ld2410_parser_feed(&p, k_basic[sizeof(k_basic) - 1], &r));
}

static void test_engineering_frame(void)
{
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r = { .moving = true, .still = true };
    CHECK_EQ(feed(&p, k_engineering, sizeof(k_engineering), &r), 1);
    CHECK(!r.moving);
    CHECK(!r.still);
}

static void test_garbage_and_resync(void)
{
    // Noise, a lone header byte, then "F4 F4 F3 F2 F1": the second F4 starts the real header.
    const uint8_t noise[] = { 0x00, 0x55, 0xF4, 0x12, 0xF4, 0xF3, 0x00, 0xF4 };
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, noise, sizeof(noise), &r), 0);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_ack_frame_ignored(void)
{
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, k_ack, sizeof(k_ack), &r), 0);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_bad_footer_rejected(void)
{
    uint8_t frame[sizeof(k_basic)];
    memcpy(frame, k_basic, sizeof(frame));
    frame[sizeof(frame) - 1] = 0x00;
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, frame, sizeof(frame), &r), 0);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_wrong_type_or_head_rejected(void)
{
    uint8_t frame[sizeof(k_basic)];
    ld2410_parser_t p;
    ld2410_report_t r;

    memcpy(frame, k_basic, sizeof(frame));
    frame[6] = 0x03;   // unknown report type
    ld2410_parser_init(&p);
    CHECK_EQ(feed(&p, frame, sizeof(frame), &r), 0);

    memcpy(frame, k_basic, sizeof(frame));
    frame[7] = 0xAB;   // head byte must be 0xAA
    ld2410_parser_init(&p);
    CHECK_EQ(feed(&p, frame, sizeof(frame), &r), 0);
}

static void test_short_payload_rejected(void)
{
    // Valid framing, but a 4-byte payload cannot hold a target report.
    const uint8_t frame[] = { 0xF4, 0xF3, 0xF2, 0xF1, 0x04, 0x00, 0x02, 0xAA, 0x00, 0x00, 0xF8, 0xF7, 0xF6, 0xF5 };
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, frame, sizeof(frame), &r), 0);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_oversized_length_rejected(void)
{
    // Length 0x0100 > LD2410_MAX_PAYLOAD: dropped at once, the next frame is decoded.
    const uint8_t frame[] = { 0xF4, 0xF3, 0xF2, 0xF1, 0x00, 0x01 };
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, frame, sizeof(frame), &r), 0);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_split_across_reads(void)
{
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    int reports = 0;
    for (size_t split = 1; split < sizeof(k_basic); split++) {
        reports += feed(&p, k_basic, split, &r);
        reports += feed(&p, k_basic + split, sizeof(k_basic) - split, &r);
    }
    CHECK_EQ(reports, (int)sizeof(k_basic) - 1);
}

static void test_truncated_frame_recovers(void)
{
    // A frame cut in the middle swallows part of the next one; the frame after that is decoded.
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    int reports = feed(&p, k_basic, 10, &r);
    reports += feed(&p, k_basic, sizeof(k_basic), &r);
    reports += feed(&p, k_basic, sizeof(k_basic), &r);
    CHECK(reports >= 1);
    CHECK_EQ(feed(&p, k_basic, sizeof(k_basic), &r), 1);
}

static void test_back_to_back_frames(void)
{
    uint8_t stream[3 * sizeof(k_basic)];
    for (int i = 0; i < 3; i++) {
        memcpy(&stream[i * sizeof(k_basic)], k_basic, sizeof(k_basic));
    }
    ld2410_parser_t p;
    ld2410_parser_init(&p);
    ld2410_report_t r;
    CHECK_EQ(feed(&p, stream, sizeof(stream), &r), 3);
}

int main(void)
{
    build_engineering();
    test_basic_frame();
    test_report_only_on_last_byte();
    test_engineering_frame();
    test_garbage_and_resync();
    test_ack_frame_ignored();
    test_bad_footer_rejected();
    test_wrong_type_or_head_rejected();
    test_short_payload_rejected();
    test_oversized_length_rejected();
    test_split_across_reads();
    test_truncated_frame_recovers();
    test_back_to_back_frames();
    return test_result("ld2410_parser");
}
