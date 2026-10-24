// BOOT button triple click (main/click_detector.cpp), sampled every 10 ms like button_handler.cpp:
// debounce, 2 s window, a hold is never several clicks, button held at start-up ignored.
#include "test.h"

#include "click_detector.h"

static constexpr uint32_t kPollMs = 10;

struct Sim {
    click_detector_t cd;
    uint32_t now = 0;
    int fired = 0;

    explicit Sim(bool pressed_at_start = false)
    {
        const click_detector_config_t cfg = { .debounce_ms = 40, .window_ms = 2000, .max_press_ms = 800,
                                              .clicks_needed = 3 };
        click_detector_init(&cd, &cfg, pressed_at_start, now);
    }

    // Holds a level for ms, one sample per poll
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

    // A press with contact bounce: the level flips every poll for bounce_ms first
    void bouncy_click(uint32_t bounce_ms = 30)
    {
        for (uint32_t t = 0; t < bounce_ms; t += kPollMs) {
            hold((t / kPollMs) % 2 == 0, kPollMs);
        }
        hold(true, 120);
        for (uint32_t t = 0; t < bounce_ms; t += kPollMs) {
            hold((t / kPollMs) % 2 == 1, kPollMs);
        }
        hold(false, 200);
    }
};

static void triple_click_fires_once()
{
    Sim s;
    s.click();
    s.click();
    CHECK(s.fired == 0);
    s.click();
    CHECK(s.fired == 1);
    s.hold(false, 3000);
    CHECK(s.fired == 1);
}

static void two_clicks_do_nothing()
{
    Sim s;
    s.click();
    s.click();
    s.hold(false, 5000);
    CHECK(s.fired == 0);
}

static void too_slow_does_not_fire()
{
    Sim s;
    s.click(120, 900);
    s.click(120, 900);
    s.click(120, 200); // third press after the 2 s window: it starts a new sequence of its own
    s.hold(false, 2500);
    CHECK(s.fired == 0);
    // A fresh fast sequence works
    s.click();
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
}

static void short_glitch_is_not_a_press()
{
    Sim s;
    for (int i = 0; i < 6; i++) {
        s.hold(true, 20); // shorter than the 40 ms debounce
        s.hold(false, 100);
    }
    CHECK(s.fired == 0);
}

static void long_hold_is_not_clicks()
{
    Sim s;
    s.hold(true, 5000); // one long hold
    s.hold(false, 300);
    CHECK(s.fired == 0);
    // Two clicks then a hold: the hold cancels the sequence
    s.click();
    s.click();
    s.hold(true, 1500);
    s.hold(false, 300);
    CHECK(s.fired == 0);
    // And a triple click afterwards still works
    s.click();
    s.click();
    s.click();
    CHECK(s.fired == 1);
}

static void pressed_at_start_is_ignored()
{
    Sim s(true);
    s.hold(true, 300); // still held after boot
    s.hold(false, 200);
    s.click();
    s.click();
    CHECK(s.fired == 0); // the boot release did not count as a click
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
}

static void window_expiry_while_pressed()
{
    Sim s;
    s.click(120, 800);
    s.click(120, 800);
    // third press starts at ~1.84 s and is released after the window: not a click of a new sequence either
    s.hold(true, 400);
    s.hold(false, 200);
    CHECK(s.fired == 0);
    s.click();
    s.click();
    CHECK(s.fired == 0);
    s.click();
    CHECK(s.fired == 1);
}

static void wraparound()
{
    Sim s;
    s.now = 0xFFFFFF00u; // millisecond counter about to wrap
    click_detector_config_t cfg = s.cd.cfg;
    click_detector_init(&s.cd, &cfg, false, s.now);
    s.click();
    s.click();
    s.click();
    CHECK(s.fired == 1);
}

int main()
{
    RUN(triple_click_fires_once);
    RUN(two_clicks_do_nothing);
    RUN(too_slow_does_not_fire);
    RUN(bounces_count_once);
    RUN(short_glitch_is_not_a_press);
    RUN(long_hold_is_not_clicks);
    RUN(pressed_at_start_is_ignored);
    RUN(four_clicks_fire_once);
    RUN(window_expiry_while_pressed);
    RUN(wraparound);
    return test_summary("click_detector");
}
