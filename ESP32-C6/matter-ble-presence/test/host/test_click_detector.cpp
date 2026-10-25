// BOOT button triple click (main/click_detector.cpp), sampled every 10 ms like button_handler.cpp, with the
// Kconfig defaults: debounce 40 ms, at most 2 s from a release to the next press, a press up to 1.5 s is a click.
// Slow clicks count, a longer pause starts again, a hold is never several clicks, button held at start-up ignored.
#include "test.h"

#include "click_detector.h"

static constexpr uint32_t kPollMs = 10;
static constexpr uint32_t kGapMs = 2000;
static constexpr uint32_t kMaxPressMs = 1500;

struct Sim {
    click_detector_t cd;
    uint32_t now = 0;
    int fired = 0;

    explicit Sim(bool pressed_at_start = false)
    {
        const click_detector_config_t cfg = { .debounce_ms = 40, .gap_ms = kGapMs, .max_press_ms = kMaxPressMs,
                                              .clicks_needed = 3 };
        click_detector_init(&cd, &cfg, pressed_at_start, now);
    }

    // Holds a level for ms, one sample per poll. A press held for press_ms then released lasts exactly press_ms
    // (from the first pressed sample to the first released one), and the pause before the next press is gap_ms.
    void hold(bool pressed, uint32_t ms)
    {
        for (uint32_t t = 0; t < ms; t += kPollMs) {
            now += kPollMs;
            fired += click_detector_update(&cd, pressed, now) ? 1 : 0;
        }
    }

    void click(uint32_t press_ms = 120, uint32_t gap_ms = 200)
    {
        hold(true, press_ms);
        hold(false, gap_ms);
    }

    // A click with contact bounce: the level flips every poll for bounce_ms first
    void bouncy_click(uint32_t gap_ms = 200, uint32_t bounce_ms = 30)
    {
        for (uint32_t t = 0; t < bounce_ms; t += kPollMs) {
            hold((t / kPollMs) % 2 == 0, kPollMs);
        }
        hold(true, 120);
        for (uint32_t t = 0; t < bounce_ms; t += kPollMs) {
            hold((t / kPollMs) % 2 == 1, kPollMs);
        }
        hold(false, gap_ms);
    }
};

static void triple_click_fires_once()
{
    Sim s;
    s.click();
    s.click();
    CHECK(s.fired == 0);
    CHECK(s.cd.clicks == 2);
    s.click();
    CHECK(s.fired == 1);
    CHECK(s.cd.clicks == 0);
    s.hold(false, 5000);
    CHECK(s.fired == 1);
}

static void two_clicks_do_nothing()
{
    Sim s;
    s.click();
    s.click();
    s.hold(false, 10000);
    CHECK(s.fired == 0);
}

static void slow_clicks_fire()
{
    // About one click every 2 s: more than the 2 s window of the earlier firmware from first press to last release
    Sim s;
    s.click(500, 1500);
    s.click(500, 1500);
    s.click(500, 1500);
    CHECK(s.fired == 1);
    // Slowest that still counts: 1.5 s presses, 2 s pauses (over 8 s in all)
    s.click(kMaxPressMs, kGapMs);
    s.click(kMaxPressMs, kGapMs);
    s.click(kMaxPressMs, kGapMs);
    CHECK(s.fired == 2);
}

static void gap_limit()
{
    // The pause is measured from the release to the next press itself: the 40 ms debounce does not shorten it
    Sim s;
    s.click(120, kGapMs);
    s.click(120, kGapMs);
    s.click(120, 300);
    CHECK(s.fired == 1);
    // 10 ms longer: the next press starts a new sequence
    s.click(120, kGapMs + kPollMs);
    s.click(120, kGapMs + kPollMs);
    CHECK(s.cd.clicks == 1);
    s.click(120, kGapMs + kPollMs);
    CHECK(s.fired == 1);
}

static void long_pause_starts_again()
{
    Sim s;
    s.click();
    s.click(120, 3000); // two clicks, then a pause longer than 2 s
    s.click();          // first click of a new sequence, not the third
    CHECK(s.fired == 0);
    CHECK(s.cd.clicks == 1);
    s.click();
    s.click();
    CHECK(s.fired == 1);
}

static void bounces_count_once()
{
    Sim s;
    s.bouncy_click();
    s.bouncy_click();
    CHECK(s.fired == 0);
    s.bouncy_click();
    CHECK(s.fired == 1);
    // Slow and bouncy: the pause runs from the end of the release bounce to the start of the press bounce
    s.bouncy_click(1900);
    s.bouncy_click(1900);
    s.bouncy_click(1900);
    CHECK(s.fired == 2);
}

static void short_glitch_is_not_a_press()
{
    Sim s;
    for (int i = 0; i < 6; i++) {
        s.hold(true, 20); // shorter than the 40 ms debounce
        s.hold(false, 100);
    }
    CHECK(s.fired == 0);
    CHECK(s.cd.clicks == 0);
}

static void long_hold_is_not_clicks()
{
    Sim s;
    s.hold(true, 8000); // one long hold
    s.hold(false, 300);
    CHECK(s.fired == 0);
    CHECK(s.cd.clicks == 0);
    // Two clicks then a press just over the limit: it cancels the sequence
    s.click();
    s.click();
    s.hold(true, kMaxPressMs + kPollMs);
    s.hold(false, 300);
    CHECK(s.fired == 0);
    CHECK(s.cd.clicks == 0);
    // A press of exactly the limit is still a click
    s.click(kMaxPressMs);
    s.click();
    s.click();
    CHECK(s.fired == 1);
}

static void pressed_at_start_is_ignored()
{
    Sim s(true);
    s.hold(true, 300); // still held after boot
    s.hold(false, 200);
    CHECK(s.cd.clicks == 0); // the boot release did not count as a click
    s.click();
    s.click();
    CHECK(s.fired == 0);
    s.click();
    CHECK(s.fired == 1);
}

static void four_clicks_fire_once()
{
    Sim s;
    for (int i = 0; i < 4; i++) {
        s.click();
    }
    CHECK(s.fired == 1);
    CHECK(s.cd.clicks == 1); // the fourth click began a new sequence
    s.click();
    s.click();
    CHECK(s.fired == 2);
}

static void wraparound()
{
    Sim s;
    s.now = 0xFFFFF000u; // millisecond counter wraps during the sequence
    click_detector_config_t cfg = s.cd.cfg;
    click_detector_init(&s.cd, &cfg, false, s.now);
    s.click(500, 1500);
    s.click(500, 1500);
    s.click(500, 1500);
    CHECK(s.fired == 1);
    CHECK(s.now < 0x1000u);
    // A long pause across the wrap still starts again
    Sim t;
    t.now = 0xFFFFF800u;
    click_detector_init(&t.cd, &cfg, false, t.now);
    t.click();
    t.click(120, 3000);
    t.click();
    CHECK(t.fired == 0);
    CHECK(t.cd.clicks == 1);
}

int main()
{
    RUN(triple_click_fires_once);
    RUN(two_clicks_do_nothing);
    RUN(slow_clicks_fire);
    RUN(gap_limit);
    RUN(long_pause_starts_again);
    RUN(bounces_count_once);
    RUN(short_glitch_is_not_a_press);
    RUN(long_hold_is_not_clicks);
    RUN(pressed_at_start_is_ignored);
    RUN(four_clicks_fire_once);
    RUN(wraparound);
    return test_summary("click_detector");
}
