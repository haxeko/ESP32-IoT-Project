// Host test of the fan state machine (main/fan_logic.c)
#include "check.h"

#include <string.h>

#include "fan_logic.h"
#include "fan_record.h"

static fan_state_t state(uint8_t speed, uint8_t last, bool swing)
{
    fan_state_t s = { speed, last, swing, false };
    return s;
}

static fan_cmd_t cmd(uint8_t type, uint8_t arg)
{
    fan_cmd_t c = { type, arg, false, false };
    return c;
}

static fan_state_t natural_state(uint8_t speed, uint8_t last, bool natural)
{
    fan_state_t s = { speed, last, false, natural };
    return s;
}

static fan_cmd_t step(uint8_t type, bool wrap, bool lowest_off)
{
    fan_cmd_t c = { type, 0, wrap, lowest_off };
    return c;
}

// The cluster derives the settings with the Fan Control rules (spec 4.4.6.3.1 / 4.4.6.6.1): a written PercentSetting
// P gives SpeedSetting = ceil(SpeedMax * P / 100); a written SpeedSetting S gives PercentSetting = floor(S * 100 /
// SpeedMax), and FanMode Low <= 33 < Medium <= 66 < High. The fan runs at SpeedSetting and reports PercentCurrent =
// fan_speed_to_percent(speed), which must agree with those rules.
static unsigned spec_speed_from_percent(unsigned percent)
{
    return (FAN_SPEED_COUNT * percent + 99) / 100;
}

static void test_percent_rules(void)
{
    CHECK_EQ(fan_speed_to_percent(0), 0);
    CHECK_EQ(fan_speed_to_percent(1), 33);
    CHECK_EQ(fan_speed_to_percent(2), 66);
    CHECK_EQ(fan_speed_to_percent(3), 100);
    CHECK_EQ(fan_speed_to_percent(9), 100);

    for (uint8_t s = 0; s <= FAN_SPEED_COUNT; s++) {
        const unsigned p = fan_speed_to_percent(s);
        CHECK_EQ(p, (unsigned)s * 100 / FAN_SPEED_COUNT); // floor(S * 100 / SpeedMax)
        CHECK_EQ(spec_speed_from_percent(p), s);           // and back: the same speed
        if (s) {
            const unsigned mode = p <= 33 ? 1 : (p <= 66 ? 2 : 3); // Low / Medium / High
            CHECK_EQ(mode, s);
        }
    }
    // Written percentages: 1-33 run speed 1, 34-66 speed 2, 67-100 speed 3 (e.g. Home Assistant's 67 % is speed 3)
    CHECK_EQ(spec_speed_from_percent(1), 1);
    CHECK_EQ(spec_speed_from_percent(33), 1);
    CHECK_EQ(spec_speed_from_percent(34), 2);
    CHECK_EQ(spec_speed_from_percent(45), 2);
    CHECK_EQ(spec_speed_from_percent(66), 2);
    CHECK_EQ(spec_speed_from_percent(67), 3);
    CHECK_EQ(spec_speed_from_percent(100), 3);
}

static void test_power(void)
{
    fan_state_t s = state(0, 2, false);
    fan_cmd_t c = cmd(FAN_CMD_TOGGLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 2); // resumes the last speed
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 0);
    CHECK_EQ(s.last_speed, 2); // kept while off

    c = cmd(FAN_CMD_ON, 0);
    s = state(3, 3, false);
    CHECK(!fan_state_apply(&s, &c)); // already running: unchanged
    CHECK_EQ(s.speed, 3);
    s = state(0, 1, true);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 1);
    CHECK(s.swing);

    c = cmd(FAN_CMD_OFF, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 0);
    CHECK(!fan_state_apply(&s, &c));

    // Nothing stored yet: first "on" uses the default speed
    s = state(0, 0, false);
    c = cmd(FAN_CMD_TOGGLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, FAN_DEFAULT_SPEED);
}

static void test_speed(void)
{
    fan_state_t s = state(0, 3, false);
    fan_cmd_t c = cmd(FAN_CMD_SPEED_CYCLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 3); // off: on at the last speed
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 1);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 2);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 3);
    CHECK_EQ(s.last_speed, 3);

    c = cmd(FAN_CMD_SET_SPEED, 2);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 2);
    CHECK_EQ(s.last_speed, 2);
    CHECK(!fan_state_apply(&s, &c));
    c = cmd(FAN_CMD_SET_SPEED, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 0);
    CHECK_EQ(s.last_speed, 2);
    c = cmd(FAN_CMD_SET_SPEED, 7);
    CHECK(fan_state_apply(&s, &c));
    CHECK_EQ(s.speed, 3);
}

static void test_swing(void)
{
    fan_state_t s = state(0, 1, false);
    fan_cmd_t c = cmd(FAN_CMD_SWING_TOGGLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.swing);
    CHECK(!fan_state_swing_running(&s)); // setting kept, relay off while the fan is off
    c = cmd(FAN_CMD_ON, 0);
    fan_state_apply(&s, &c);
    CHECK(fan_state_swing_running(&s));
    c = cmd(FAN_CMD_SET_SWING, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(!s.swing);
    CHECK(!fan_state_apply(&s, &c));
    CHECK(!fan_state_swing_running(&s));
    CHECK_EQ(s.speed, 1); // speed unchanged
}

// Oscillation relay: for every state and every command, it is only closed while the fan runs (the app may show the
// oscillation setting as on while the fan is off: that is the setting for the next start, the relay stays open)
static void test_swing_relay_never_on_while_off(void)
{
    const uint8_t types[] = { FAN_CMD_TOGGLE,         FAN_CMD_ON,          FAN_CMD_OFF,       FAN_CMD_SPEED_CYCLE,
                              FAN_CMD_SET_SPEED,      FAN_CMD_SWING_TOGGLE, FAN_CMD_SET_SWING, FAN_CMD_STEP_UP,
                              FAN_CMD_STEP_DOWN,      FAN_CMD_NATURAL_TOGGLE, FAN_CMD_SET_NATURAL };
    for (uint8_t speed = 0; speed <= FAN_SPEED_COUNT; speed++) {
        for (uint8_t last = 1; last <= FAN_SPEED_COUNT; last++) {
            for (int flags = 0; flags < 4; flags++) {
                for (unsigned t = 0; t < sizeof(types); t++) {
                    for (uint8_t arg = 0; arg <= FAN_SPEED_COUNT; arg++) {
                        for (int opt = 0; opt < 4; opt++) {
                            fan_state_t s = { speed, last, (flags & 1) != 0, (flags & 2) != 0 };
                            fan_cmd_t c = { types[t], arg, (opt & 1) != 0, (opt & 2) != 0 };
                            fan_state_apply(&s, &c);
                            CHECK(fan_state_swing_running(&s) == (s.swing && s.speed > 0));
                            if (s.speed == 0) {
                                CHECK(!fan_state_swing_running(&s));
                                CHECK(!fan_state_natural_running(&s));
                            }
                            CHECK(s.last_speed >= 1 && s.last_speed <= FAN_SPEED_COUNT);
                            if (s.speed) {
                                CHECK_EQ(s.last_speed, s.speed);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void test_step(void)
{
    fan_state_t s = state(0, 2, false);
    fan_cmd_t up = step(FAN_CMD_STEP_UP, false, true);
    fan_cmd_t down = step(FAN_CMD_STEP_DOWN, false, true);

    CHECK(fan_state_apply(&s, &up));
    CHECK_EQ(s.speed, 1); // from off: lowest speed
    fan_state_apply(&s, &up);
    fan_state_apply(&s, &up);
    CHECK_EQ(s.speed, 3);
    CHECK(!fan_state_apply(&s, &up)); // no wrap: stays at 3
    fan_state_apply(&s, &down);
    fan_state_apply(&s, &down);
    CHECK_EQ(s.speed, 1);
    CHECK(fan_state_apply(&s, &down));
    CHECK_EQ(s.speed, 0); // LowestOff: down from 1 turns off
    CHECK(!fan_state_apply(&s, &down));
    CHECK_EQ(s.last_speed, 1);

    // Wrap
    fan_cmd_t up_wrap = step(FAN_CMD_STEP_UP, true, true);
    s = state(3, 3, false);
    fan_state_apply(&s, &up_wrap);
    CHECK_EQ(s.speed, 0);
    fan_cmd_t up_wrap_on = step(FAN_CMD_STEP_UP, true, false);
    s = state(3, 3, false);
    fan_state_apply(&s, &up_wrap_on);
    CHECK_EQ(s.speed, 1);
    fan_cmd_t down_wrap_on = step(FAN_CMD_STEP_DOWN, true, false);
    s = state(1, 1, false);
    fan_state_apply(&s, &down_wrap_on);
    CHECK_EQ(s.speed, 3);

    // LowestOff false: down never turns the fan off
    fan_cmd_t down_on = step(FAN_CMD_STEP_DOWN, false, false);
    s = state(1, 1, false);
    CHECK(!fan_state_apply(&s, &down_on));
    CHECK_EQ(s.speed, 1);
}

static void test_sanitize(void)
{
    fan_state_t s = state(9, 0, true);
    fan_state_sanitize(&s);
    CHECK_EQ(s.speed, 0);
    CHECK_EQ(s.last_speed, FAN_DEFAULT_SPEED);
    s = state(2, 3, false);
    fan_state_sanitize(&s);
    CHECK_EQ(s.speed, 2);
    CHECK_EQ(s.last_speed, 2);
    s = state(0, 3, false);
    fan_state_sanitize(&s);
    CHECK_EQ(s.last_speed, 3);

    fan_cmd_t bad = cmd(200, 0);
    s = state(1, 1, false);
    CHECK(!fan_state_apply(&s, &bad));
}

static void test_natural(void)
{
    // Natural wind on: the fan starts at its last speed when off
    fan_state_t s = natural_state(0, 2, false);
    fan_cmd_t c = cmd(FAN_CMD_SET_NATURAL, 1);
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.natural);
    CHECK_EQ(s.speed, 2);
    CHECK(fan_state_natural_running(&s));
    CHECK(!fan_state_apply(&s, &c)); // already on

    // Off keeps the setting, On / Toggle resume natural wind
    c = cmd(FAN_CMD_OFF, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.natural);
    CHECK(!fan_state_natural_running(&s));
    c = cmd(FAN_CMD_TOGGLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(fan_state_natural_running(&s));
    c = cmd(FAN_CMD_SET_SPEED, 0); // Matter FanMode Off
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.natural);
    c = cmd(FAN_CMD_ON, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(fan_state_natural_running(&s));

    // Oscillation does not change it
    c = cmd(FAN_CMD_SWING_TOGGLE, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.natural);

    // Every speed setting is manual: natural wind ends, the fan keeps running
    const uint8_t manual[] = { FAN_CMD_SET_SPEED, FAN_CMD_SPEED_CYCLE, FAN_CMD_STEP_UP, FAN_CMD_STEP_DOWN };
    for (unsigned i = 0; i < sizeof(manual); i++) {
        s = natural_state(2, 2, true);
        c = cmd(manual[i], 3);
        CHECK(fan_state_apply(&s, &c));
        CHECK(!s.natural);
        CHECK(s.speed > 0);
    }
    // Setting the speed it already had still ends natural wind
    s = natural_state(3, 3, true);
    c = cmd(FAN_CMD_SET_SPEED, 3);
    CHECK(fan_state_apply(&s, &c));
    CHECK(!s.natural);
    CHECK_EQ(s.speed, 3);
    // A Step down to off is off: natural wind is kept
    s = natural_state(1, 1, true);
    fan_cmd_t st = step(FAN_CMD_STEP_DOWN, false, true);
    CHECK(fan_state_apply(&s, &st));
    CHECK_EQ(s.speed, 0);
    CHECK(s.natural);

    // Off: manual, the fan keeps its speed
    s = natural_state(1, 1, true);
    c = cmd(FAN_CMD_SET_NATURAL, 0);
    CHECK(fan_state_apply(&s, &c));
    CHECK(!s.natural);
    CHECK_EQ(s.speed, 1);
    CHECK(!fan_state_apply(&s, &c));

    // Toggle: running natural -> manual; manual or off -> natural (and on)
    c = cmd(FAN_CMD_NATURAL_TOGGLE, 0);
    s = natural_state(2, 2, true);
    CHECK(fan_state_apply(&s, &c));
    CHECK(!s.natural);
    CHECK_EQ(s.speed, 2);
    CHECK(fan_state_apply(&s, &c));
    CHECK(s.natural);
    CHECK_EQ(s.speed, 2);
    s = natural_state(0, 3, true); // off with natural wind set: turns on
    CHECK(fan_state_apply(&s, &c));
    CHECK(fan_state_natural_running(&s));
    CHECK_EQ(s.speed, 3);
    s = natural_state(0, 1, false);
    CHECK(fan_state_apply(&s, &c));
    CHECK(fan_state_natural_running(&s));
}

static void test_natural_pattern(void)
{
    uint32_t rng = 12345;
    unsigned count[FAN_SPEED_COUNT + 1] = { 0 };
    unsigned long hold_sum = 0;
    uint8_t speed = 0;
    const int steps = 20000;
    for (int i = 0; i < steps; i++) {
        uint32_t hold_ms = 0;
        const uint8_t next = fan_natural_next(&rng, speed, 8, 30, &hold_ms);
        CHECK(next >= 1 && next <= FAN_SPEED_COUNT);
        if (i == 0) {
            CHECK(next <= 2); // starts low
        }
        CHECK(next != speed); // always a change
        CHECK(hold_ms >= 8000);
        CHECK(hold_ms <= (next == 3 ? 19000u : 30000u)); // gusts are short
        count[next]++;
        hold_sum += hold_ms;
        speed = next;
    }
    // Every speed is used, speed 2 the most
    for (int sp = 1; sp <= FAN_SPEED_COUNT; sp++) {
        CHECK(count[sp] > (unsigned)steps / 10);
    }
    CHECK(count[2] > count[1]);
    CHECK(count[2] > count[3]);
    const unsigned long avg_ms = hold_sum / steps;
    CHECK(avg_ms > 12000 && avg_ms < 20000);

    // Limits: max below min, zero min, zero generator state
    uint32_t hold_ms = 0;
    rng = 0;
    CHECK(fan_natural_next(&rng, 2, 10, 5, &hold_ms) != 2);
    CHECK_EQ(hold_ms, 10000);
    CHECK(rng != 0);
    fan_natural_next(&rng, 1, 0, 0, &hold_ms);
    CHECK_EQ(hold_ms, 1000);
    fan_natural_next(&rng, 9, 600, 600, &hold_ms);
    CHECK_EQ(hold_ms, 600000);
}

// NVS record of the fan state: byte layout, older and newer versions (OTA rollback), invalid records
static void test_record(void)
{
    // Layout: version, speed, last speed, oscillation, natural wind (unchanged since version 2)
    uint8_t buf[FAN_RECORD_MAX_SIZE];
    fan_state_t in = { 2, 2, true, true };
    fan_record_encode(&in, buf);
    const uint8_t expected[FAN_RECORD_SIZE] = { 2, 2, 2, 1, 1 };
    CHECK(memcmp(buf, expected, sizeof(expected)) == 0);
    fan_state_t out = { 0, 1, false, false };
    CHECK(fan_record_decode(buf, FAN_RECORD_SIZE, &out));
    CHECK(out.speed == 2 && out.last_speed == 2 && out.swing && out.natural);
    in = (fan_state_t){ 0, 3, false, false };
    fan_record_encode(&in, buf);
    CHECK(fan_record_decode(buf, FAN_RECORD_SIZE, &out));
    CHECK(out.speed == 0 && out.last_speed == 3 && !out.swing && !out.natural);

    // Version 1 (4 bytes, no natural wind)
    const uint8_t v1[] = { 1, 3, 3, 1 };
    out = (fan_state_t){ 0, 1, false, true };
    CHECK(fan_record_decode(v1, sizeof(v1), &out));
    CHECK(out.speed == 3 && out.last_speed == 3 && out.swing && !out.natural);

    // A newer, longer record (fields appended): the known fields are read
    const uint8_t v3[] = { 3, 1, 1, 0, 1, 0xAA, 0xBB, 0xCC };
    CHECK(fan_record_decode(v3, sizeof(v3), &out));
    CHECK(out.speed == 1 && out.last_speed == 1 && !out.swing && out.natural);

    // Invalid records leave the state unchanged
    const fan_state_t before = { 2, 2, true, false };
    const uint8_t v1_long[] = { 1, 3, 3, 1, 1 };  // version 1 is exactly 4 bytes
    const uint8_t v2_short[] = { 2, 3, 3, 1 };    // version 2 needs 5 bytes
    const uint8_t v0[] = { 0, 1, 1, 0, 0 };
    const uint8_t *bad[] = { v1_long, v2_short, v0 };
    const size_t bad_len[] = { sizeof(v1_long), sizeof(v2_short), sizeof(v0) };
    for (unsigned i = 0; i < 3; i++) {
        out = before;
        CHECK(!fan_record_decode(bad[i], bad_len[i], &out));
        CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    }
    CHECK(!fan_record_decode(v1, 0, &out));
    CHECK(!fan_record_decode(NULL, 5, &out));

    // Out-of-range values are sanitized
    const uint8_t odd[] = { 2, 9, 0, 7, 9 };
    CHECK(fan_record_decode(odd, sizeof(odd), &out));
    CHECK(out.speed == 0 && out.last_speed == FAN_DEFAULT_SPEED && out.swing && out.natural);
}

int main(void)
{
    test_percent_rules();
    test_power();
    test_speed();
    test_swing();
    test_swing_relay_never_on_while_off();
    test_step();
    test_sanitize();
    test_natural();
    test_natural_pattern();
    test_record();
    return test_result("fan_logic");
}
