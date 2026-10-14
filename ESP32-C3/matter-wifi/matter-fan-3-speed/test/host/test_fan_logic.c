// Host test of the fan state machine (main/fan_logic.c)
#include "check.h"

#include "fan_logic.h"

static fan_state_t state(uint8_t speed, uint8_t last, bool swing)
{
    fan_state_t s = { speed, last, swing };
    return s;
}

static fan_cmd_t cmd(uint8_t type, uint8_t arg)
{
    fan_cmd_t c = { type, arg, false, false };
    return c;
}

static fan_cmd_t step(uint8_t type, bool wrap, bool lowest_off)
{
    fan_cmd_t c = { type, 0, wrap, lowest_off };
    return c;
}

static void test_percent_mapping(void)
{
    // The three slider positions, and the Matter values that FanMode Low/Medium/High set
    CHECK_EQ(fan_speed_to_percent(0), 0);
    CHECK_EQ(fan_speed_to_percent(1), 33);
    CHECK_EQ(fan_speed_to_percent(2), 66);
    CHECK_EQ(fan_speed_to_percent(3), 100);
    CHECK_EQ(fan_speed_to_percent(9), 100);

    // Every percent snaps to the nearest position; only 0 is off
    CHECK_EQ(fan_percent_to_speed(0), 0);
    for (int p = 1; p <= 49; p++) {
        CHECK_EQ(fan_percent_to_speed((uint8_t)p), 1);
    }
    for (int p = 50; p <= 83; p++) {
        CHECK_EQ(fan_percent_to_speed((uint8_t)p), 2);
    }
    for (int p = 84; p <= 100; p++) {
        CHECK_EQ(fan_percent_to_speed((uint8_t)p), 3);
    }
    CHECK_EQ(fan_percent_to_speed(255), 3);

    // Home Assistant sends 33 / 67 / 100 for a 3-speed fan (Math.round of n * 33.33)
    CHECK_EQ(fan_percent_to_speed(67), 2);

    // Round trip: a snapped value stays where it is
    for (uint8_t s = 0; s <= FAN_SPEED_COUNT; s++) {
        CHECK_EQ(fan_percent_to_speed(fan_speed_to_percent(s)), s);
    }

    // The Matter cluster derives SpeedSetting = ceil(SpeedMax * percent / 100) and FanMode Low <= 33 < Medium <= 66 <
    // High from the snapped percent: they agree with the speed
    for (uint8_t s = 1; s <= FAN_SPEED_COUNT; s++) {
        unsigned p = fan_speed_to_percent(s);
        CHECK_EQ((FAN_SPEED_COUNT * p + 99) / 100, s);
        unsigned mode = p <= 33 ? 1 : (p <= 66 ? 2 : 3);
        CHECK_EQ(mode, s);
    }
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

int main(void)
{
    test_percent_mapping();
    test_power();
    test_speed();
    test_swing();
    test_step();
    test_sanitize();
    return test_result("fan_logic");
}
