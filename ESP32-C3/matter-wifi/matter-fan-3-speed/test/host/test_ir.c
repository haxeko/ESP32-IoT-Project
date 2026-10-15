// Host test of the IR decoder and the learned-button table (main/ir_decode.c, main/ir_table.c)
#include "check.h"

#include <string.h>

#include "ir_decode.h"
#include "ir_table.h"

/* Deterministic timing noise in [-range, +range] */
static unsigned s_seed = 1;
static int jitter(int range)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (int)((s_seed >> 16) % (unsigned)(2 * range + 1)) - range;
}

static uint16_t dur(int us)
{
    return (uint16_t)(us < 1 ? 1 : us);
}

/* NEC frame as seen through an IR receiver module: marks stretched, spaces shortened by `stretch` us */
static size_t nec_frame(ir_pulse_t *p, uint32_t value, int stretch, int noise)
{
    size_t n = 0;
    p[n].mark_us = dur(9000 + stretch + jitter(noise * 4));
    p[n++].space_us = dur(4500 - stretch + jitter(noise * 2));
    for (int i = 0; i < 32; i++) {
        p[n].mark_us = dur(560 + stretch + jitter(noise));
        p[n++].space_us = dur(((value >> i) & 1 ? 1690 : 560) - stretch + jitter(noise));
    }
    p[n].mark_us = dur(560 + stretch + jitter(noise));
    p[n++].space_us = 0;
    return n;
}

static size_t nec_repeat(ir_pulse_t *p)
{
    p[0].mark_us = 9050;
    p[0].space_us = 2200;
    p[1].mark_us = 600;
    p[1].space_us = 0;
    return 2;
}

/* Sony SIRC 12-bit: 2400 us header, bit = 600 us (0) or 1200 us (1) mark, 600 us spaces */
static size_t sony_frame(ir_pulse_t *p, uint16_t value, int noise)
{
    size_t n = 0;
    p[n].mark_us = dur(2400 + jitter(noise));
    p[n++].space_us = dur(600 + jitter(noise));
    for (int i = 0; i < 12; i++) {
        p[n].mark_us = dur(((value >> i) & 1 ? 1200 : 600) + jitter(noise));
        p[n++].space_us = dur(600 + jitter(noise));
    }
    p[n - 1].space_us = 0;
    return n;
}

/* NEC-like frame with a 4500/4500 leader (Samsung): not NEC, identified by its shape */
static size_t samsung_frame(ir_pulse_t *p, uint32_t value, int noise)
{
    size_t n = nec_frame(p, value, 0, noise);
    p[0].mark_us = dur(4500 + jitter(noise));
    return n;
}

static void test_nec(void)
{
    ir_pulse_t p[40];
    ir_code_t code;
    const uint32_t value = 0xBA45FF00; /* address 0x00, command 0x45 (common "power" key) */

    size_t n = nec_frame(p, value, 0, 0);
    CHECK_EQ(ir_decode(p, n, &code), IR_FRAME_CODE);
    CHECK_EQ(code.proto, IR_PROTO_NEC);
    CHECK_EQ(code.value, value);
    CHECK_EQ(code.pulses, 34);

    /* Receiver distortion and timing noise */
    for (int k = 0; k < 200; k++) {
        n = nec_frame(p, value ^ (uint32_t)k, (k % 3) * 70, 80);
        memset(&code, 0, sizeof(code));
        CHECK_EQ(ir_decode(p, n, &code), IR_FRAME_CODE);
        CHECK_EQ(code.proto, IR_PROTO_NEC);
        CHECK_EQ(code.value, value ^ (uint32_t)k);
    }

    n = nec_repeat(p);
    CHECK_EQ(ir_decode(p, n, &code), IR_FRAME_REPEAT);

    /* A truncated NEC frame is not decoded as NEC */
    n = nec_frame(p, value, 0, 0);
    CHECK(ir_decode(p, 20, &code) != IR_FRAME_CODE || code.proto != IR_PROTO_NEC);
    /* A broken bit space makes it a shape code, consistently */
    p[10].space_us = 3000;
    CHECK_EQ(ir_decode(p, n, &code), IR_FRAME_CODE);
    CHECK_EQ(code.proto, IR_PROTO_SHAPE);
}

static void test_shape(void)
{
    ir_pulse_t p[40];
    ir_code_t a, b, c;

    /* Sony: the same button gives the same code despite noise, other buttons other codes */
    size_t n = sony_frame(p, 0x095, 0);
    CHECK_EQ(ir_decode(p, n, &a), IR_FRAME_CODE);
    CHECK_EQ(a.proto, IR_PROTO_SHAPE);
    CHECK_EQ(a.pulses, 13);
    for (int k = 0; k < 100; k++) {
        n = sony_frame(p, 0x095, 60);
        CHECK_EQ(ir_decode(p, n, &b), IR_FRAME_CODE);
        CHECK(ir_code_equal(&a, &b));
    }
    for (uint16_t v = 0; v < 64; v++) {
        if (v == 0x15) { /* 0x095 & 0x3F */
            continue;
        }
        n = sony_frame(p, (uint16_t)(0x080 | v), 0);
        CHECK_EQ(ir_decode(p, n, &c), IR_FRAME_CODE);
        CHECK(!ir_code_equal(&a, &c));
    }

    /* Samsung-like frames */
    n = samsung_frame(p, 0xE0E040BF, 0);
    CHECK_EQ(ir_decode(p, n, &a), IR_FRAME_CODE);
    CHECK_EQ(a.proto, IR_PROTO_SHAPE);
    n = samsung_frame(p, 0xE0E040BF, 70);
    CHECK_EQ(ir_decode(p, n, &b), IR_FRAME_CODE);
    CHECK(ir_code_equal(&a, &b));
    n = samsung_frame(p, 0xE0E0E01F, 0);
    CHECK_EQ(ir_decode(p, n, &c), IR_FRAME_CODE);
    CHECK(!ir_code_equal(&a, &c));

    /* Noise bursts are ignored */
    n = sony_frame(p, 0x095, 0);
    CHECK_EQ(ir_decode(p, IR_SHAPE_MIN_PULSES - 1, &c), IR_FRAME_NONE);
    CHECK_EQ(ir_decode(p, 0, &c), IR_FRAME_NONE);
    CHECK_EQ(ir_decode(NULL, 5, &c), IR_FRAME_NONE);
}

/* rmt_symbol_word_t.val: duration0 bits 0-14, level0 bit 15, duration1 bits 16-30, level1 bit 31 */
static uint32_t rmt_word(unsigned d0, unsigned l0, unsigned d1, unsigned l1)
{
    return (d0 & 0x7FFF) | (l0 << 15) | ((uint32_t)(d1 & 0x7FFF) << 16) | ((uint32_t)l1 << 31);
}

static void test_rmt_conversion(void)
{
    ir_pulse_t frame[40], back[40];
    uint32_t words[64];
    ir_code_t code;

    /* NEC frame as the RMT records it: (mark low, space high) per symbol, the last space is the 0 end marker */
    size_t n = nec_frame(frame, 0xE11EFB04, 50, 40);
    for (size_t i = 0; i < n; i++) {
        words[i] = rmt_word(frame[i].mark_us, 0, frame[i].space_us, 1);
    }
    size_t m = ir_pulses_from_rmt(words, n, back, 40);
    CHECK_EQ(m, n);
    CHECK(memcmp(frame, back, n * sizeof(ir_pulse_t)) == 0);
    CHECK_EQ(ir_decode(back, m, &code), IR_FRAME_CODE);
    CHECK_EQ(code.proto, IR_PROTO_NEC);
    CHECK_EQ(code.value, 0xE11EFB04);

    /* Idle level before the first mark is dropped */
    words[0] = rmt_word(300, 1, 9000, 0);
    words[1] = rmt_word(4500, 1, 560, 0);
    words[2] = rmt_word(0, 1, 0, 0);
    m = ir_pulses_from_rmt(words, 3, back, 40);
    CHECK_EQ(m, 2);
    CHECK_EQ(back[0].mark_us, 9000);
    CHECK_EQ(back[0].space_us, 4500);
    CHECK_EQ(back[1].mark_us, 560);
    CHECK_EQ(back[1].space_us, 0);

    /* Segments of the same level are merged (a long level split over two halves) */
    words[0] = rmt_word(5000, 0, 4000, 0);
    words[1] = rmt_word(1000, 1, 2000, 1);
    words[2] = rmt_word(600, 0, 0, 1);
    m = ir_pulses_from_rmt(words, 3, back, 40);
    CHECK_EQ(m, 2);
    CHECK_EQ(back[0].mark_us, 9000);
    CHECK_EQ(back[0].space_us, 3000);
    CHECK_EQ(back[1].mark_us, 600);

    /* Saturation instead of wrap-around: 3 x 30000 us of mark */
    words[0] = rmt_word(30000, 0, 30000, 0);
    words[1] = rmt_word(30000, 0, 30000, 1);
    words[2] = rmt_word(0, 0, 0, 0);
    m = ir_pulses_from_rmt(words, 3, back, 40);
    CHECK_EQ(m, 1);
    CHECK_EQ(back[0].mark_us, 0xFFFF);
    CHECK_EQ(back[0].space_us, 30000);

    /* No end marker: the frame ends with the last symbol */
    words[0] = rmt_word(560, 0, 560, 1);
    words[1] = rmt_word(560, 0, 1690, 1);
    m = ir_pulses_from_rmt(words, 2, back, 40);
    CHECK_EQ(m, 2);
    CHECK_EQ(back[1].space_us, 1690);

    /* Truncated to the pulse buffer, never written past it */
    for (size_t i = 0; i < 64; i++) {
        words[i] = rmt_word(560, 0, 560, 1);
    }
    memset(back, 0xAB, sizeof(back));
    m = ir_pulses_from_rmt(words, 64, back, 10);
    CHECK_EQ(m, 10);
    CHECK_EQ(back[10].mark_us, 0xABAB);
    words[9] = rmt_word(560, 0, 0, 1); /* trailing mark exactly at the limit */
    m = ir_pulses_from_rmt(words, 64, back, 10);
    CHECK_EQ(m, 10);
    CHECK_EQ(back[9].space_us, 0);

    /* Nothing received */
    CHECK_EQ(ir_pulses_from_rmt(words, 0, back, 10), 0);
    words[0] = rmt_word(0, 0, 0, 0);
    CHECK_EQ(ir_pulses_from_rmt(words, 1, back, 10), 0);
}

static void test_press_filter(void)
{
    ir_press_filter_t f;
    memset(&f, 0, sizeof(f));
    ir_code_t a = { IR_PROTO_NEC, 34, 0, 0x11 };
    ir_code_t b = { IR_PROTO_NEC, 34, 0, 0x22 };

    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 1000));
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 1100));   /* same press, sent twice */
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_REPEAT, NULL, 1210)); /* held */
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_REPEAT, NULL, 1320));
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 1500));   /* within the gap of the last repeat */
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_NONE, NULL, 1600)); /* noise does not extend the press... */
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 1500 + IR_PRESS_GAP_MS)); /* ...so this is a new press */
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &b, 1720 + IR_PRESS_GAP_MS)); /* another button, after a gap */
    CHECK(ir_code_equal(&f.last, &b));

    /* Remotes sending two different frames per press (Sharp: frame, inverted frame), held: one press only */
    memset(&f, 0, sizeof(f));
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 3000));
    for (uint32_t t = 3040; t < 4000; t += 40) {
        CHECK(!ir_press_filter_accept(&f, IR_FRAME_CODE, ((t / 40) % 2) ? &b : &a, t));
    }
    CHECK(ir_code_equal(&f.last, &a));
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 4200));

    /* A repeat frame never starts a press, even long after */
    memset(&f, 0, sizeof(f));
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_REPEAT, NULL, 5000));
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 5100)); /* still the same (half-received) press */
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 5100 + IR_PRESS_GAP_MS));

    /* Millisecond counter wrap-around */
    memset(&f, 0, sizeof(f));
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 0xFFFFFFF0u));
    CHECK(!ir_press_filter_accept(&f, IR_FRAME_CODE, &a, 0x40u));
    CHECK(ir_press_filter_accept(&f, IR_FRAME_CODE, &b, 0x40u + IR_PRESS_GAP_MS));
}

static void test_table(void)
{
    ir_table_t t;
    memset(&t, 0, sizeof(t));
    ir_code_t power = { IR_PROTO_NEC, 34, 0, 0xBA45FF00 };
    ir_code_t speed = { IR_PROTO_SHAPE, 13, 0, 0x12345678 };
    ir_code_t off = { IR_PROTO_NEC, 34, 0, 0xB847FF00 };
    ir_code_t unknown = { IR_PROTO_NEC, 34, 0, 0x01 };
    fan_cmd_t cmd;

    CHECK_EQ(ir_table_count(&t), 0);
    CHECK_EQ(ir_table_find(&t, &power), -1);
    /* Empty slots never match, not even an all-zero code */
    ir_code_t zero;
    memset(&zero, 0, sizeof(zero));
    CHECK_EQ(ir_table_find(&t, &zero), -1);

    ir_table_assign(&t, IR_FUNC_POWER, &power);
    ir_table_assign(&t, IR_FUNC_SPEED, &speed);
    CHECK_EQ(ir_table_count(&t), 2);
    CHECK_EQ(ir_table_find(&t, &power), IR_FUNC_POWER);
    CHECK_EQ(ir_table_find(&t, &speed), IR_FUNC_SPEED);
    CHECK_EQ(ir_table_find(&t, &unknown), -1);

    /* POWER toggles while no OFF button is learned */
    CHECK(ir_table_command(&t, IR_FUNC_POWER, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_TOGGLE);
    ir_table_assign(&t, IR_FUNC_OFF, &off);
    CHECK(ir_table_command(&t, IR_FUNC_POWER, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_ON);
    CHECK(ir_table_command(&t, IR_FUNC_OFF, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_OFF);

    /* Re-learning a code for another function moves it */
    ir_table_assign(&t, IR_FUNC_SWING, &speed);
    CHECK_EQ(ir_table_find(&t, &speed), IR_FUNC_SWING);
    CHECK_EQ(t.codes[IR_FUNC_SPEED].proto, IR_PROTO_NONE);
    CHECK_EQ(ir_table_count(&t), 3);

    CHECK(ir_table_command(&t, IR_FUNC_SPEED, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_SPEED_CYCLE);
    CHECK(ir_table_command(&t, IR_FUNC_SWING, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_SWING_TOGGLE);
    for (int i = 0; i < 3; i++) {
        CHECK(ir_table_command(&t, IR_FUNC_SPEED_1 + i, &cmd));
        CHECK_EQ(cmd.type, FAN_CMD_SET_SPEED);
        CHECK_EQ(cmd.arg, i + 1);
    }
    CHECK(ir_table_command(&t, IR_FUNC_NATURAL, &cmd));
    CHECK_EQ(cmd.type, FAN_CMD_NATURAL_TOGGLE);
    CHECK(strcmp(ir_func_name(IR_FUNC_NATURAL), "NATURAL") == 0);
    CHECK(!ir_table_command(&t, IR_FUNC_COUNT, &cmd));
    CHECK(!ir_table_command(&t, -1, &cmd));
    ir_table_assign(&t, IR_FUNC_COUNT, &unknown); /* ignored */
    CHECK_EQ(ir_table_find(&t, &unknown), -1);

    CHECK(strcmp(ir_func_name(IR_FUNC_SWING), "SWING") == 0);
    CHECK(strcmp(ir_func_name(99), "?") == 0);

    /* NVS layout: functions are only appended (the 7 of version 1 keep their index) */
    CHECK_EQ(IR_FUNC_SPEED_3, 6);
    CHECK_EQ(IR_FUNC_NATURAL, 7);
    CHECK_EQ(sizeof(ir_code_t), 8);
    CHECK_EQ(sizeof(ir_table_t), 8 * IR_FUNC_COUNT);
}

// NVS record of the learned codes: layout, version 1 (7 functions), newer and longer records, invalid records
static void test_record(void)
{
    CHECK_EQ(IR_RECORD_SIZE, 68);
    ir_table_t t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        ir_code_t c = { IR_PROTO_NEC, 34, 0, 0x1000u + (uint32_t)i };
        ir_table_assign(&t, i, &c);
    }
    uint8_t buf[IR_RECORD_SIZE + 8];
    ir_record_encode(&t, buf);
    // Layout: little-endian version, then the codes as stored in the table
    CHECK(buf[0] == IR_RECORD_VERSION && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    CHECK(memcmp(buf + 4, t.codes, sizeof(t.codes)) == 0);
    ir_table_t out;
    memset(&out, 0xFF, sizeof(out));
    CHECK(ir_record_decode(buf, IR_RECORD_SIZE, &out));
    CHECK(memcmp(&out, &t, sizeof(t)) == 0);

    // Version 1: 7 functions, NATURAL not learned
    uint8_t v1[4 + 7 * 8];
    memcpy(v1, buf, sizeof(v1));
    v1[0] = 1;
    memset(&out, 0xFF, sizeof(out));
    CHECK(ir_record_decode(v1, sizeof(v1), &out));
    CHECK(memcmp(out.codes, t.codes, 7 * sizeof(ir_code_t)) == 0);
    CHECK_EQ(out.codes[IR_FUNC_NATURAL].proto, IR_PROTO_NONE);
    CHECK_EQ(ir_table_count(&out), 7);

    // A newer record with one more function appended: the known ones are read
    buf[0] = 3;
    memset(buf + IR_RECORD_SIZE, 0x5A, 8);
    memset(&out, 0, sizeof(out));
    CHECK(ir_record_decode(buf, IR_RECORD_SIZE + 8, &out));
    CHECK(memcmp(&out, &t, sizeof(t)) == 0);
    buf[0] = IR_RECORD_VERSION;

    // Invalid records leave the table unchanged
    ir_table_t keep;
    memset(&keep, 0x33, sizeof(keep));
    out = keep;
    CHECK(!ir_record_decode(buf, IR_RECORD_SIZE - 8, &out)); // version 2, too short
    uint8_t v1_long[4 + 8 * 8];
    memcpy(v1_long, buf, sizeof(v1_long));
    v1_long[0] = 1;
    CHECK(!ir_record_decode(v1_long, sizeof(v1_long), &out)); // version 1 is exactly 7 functions
    uint8_t v0[IR_RECORD_SIZE];
    memcpy(v0, buf, sizeof(v0));
    v0[0] = 0;
    CHECK(!ir_record_decode(v0, sizeof(v0), &out));
    CHECK(!ir_record_decode(buf, 3, &out));
    CHECK(!ir_record_decode(NULL, IR_RECORD_SIZE, &out));
    CHECK(memcmp(&out, &keep, sizeof(keep)) == 0);
}

int main(void)
{
    test_nec();
    test_shape();
    test_rmt_conversion();
    test_press_filter();
    test_table();
    test_record();
    return test_result("ir");
}
