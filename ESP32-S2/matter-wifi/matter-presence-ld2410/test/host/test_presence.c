// Host tests of the radar -> presence decisions (main/presence.c)
#include "check.h"
#include "presence.h"

static void test_fresh(void)
{
    const int64_t timeout = 10000;
    // No frame yet: grace period counted from boot
    CHECK(presence_radar_fresh(false, 0, 100, timeout));
    CHECK(presence_radar_fresh(false, 0, timeout, timeout));
    CHECK(!presence_radar_fresh(false, 0, timeout + 1, timeout));
    // Frames received
    CHECK(presence_radar_fresh(true, 50000, 55000, timeout));
    CHECK(presence_radar_fresh(true, 50000, 60000, timeout));
    CHECK(!presence_radar_fresh(true, 50000, 60001, timeout));
}

static void test_detected(void)
{
    ld2410_report_t r = { 0 };
    CHECK(!presence_detected(&r, 0));
    CHECK(!presence_detected(&r, 150));

    r.moving = true;
    r.moving_cm = 300;
    CHECK(presence_detected(&r, 0));      // radar gates only
    CHECK(!presence_detected(&r, 150));   // too far
    CHECK(presence_detected(&r, 300));    // limit included

    r.moving = false;
    r.still = true;
    r.still_cm = 80;
    CHECK(presence_detected(&r, 150));
    CHECK(!presence_detected(&r, 79));

    // A far moving target does not hide a close still one
    r.moving = true;
    r.moving_cm = 500;
    CHECK(presence_detected(&r, 150));
}

int main(void)
{
    test_fresh();
    test_detected();
    return test_result("presence");
}
