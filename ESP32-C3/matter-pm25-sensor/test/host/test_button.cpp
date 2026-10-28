// Host test of the polled, debounced buttons (main/drivers/button.cpp): the
// main button (GPIO10, as on the PCB) and an added display button (GPIO20).
#include "sim.h"

#include "button.cpp"

static constexpr int kMain = 10;
static constexpr int kDisplay = 20;

struct Event {
    int gpio;
    button_event_t e;
};
static std::vector<Event> s_events;

// user_data is the GPIO, to tell the buttons apart.
static void on_button(const button_event_t *event, void *user_data)
{
    s_events.push_back({ (int)(intptr_t)user_data, *event });
}

// Runs the 50 ms poll of every button over the given levels of one GPIO (one
// per poll, 0 = pressed); the other buttons stay as they are.
static void poll(int gpio, const std::vector<int> &levels)
{
    for (int level : levels) {
        g_sim.gpio_level[gpio] = level;
        g_sim.now_us += BUTTON_POLL_MS * 1000;
        for (const SimTimer &t : g_sim.timers) {
            t.cb(t.arg);
        }
    }
}

static std::vector<int> press(int pressed_polls, int released_polls)
{
    std::vector<int> v(pressed_polls, 0);
    v.insert(v.end(), released_polls, 1);
    return v;
}

// n short presses (150 ms) with 200 ms between them, then 1 s released.
static std::vector<int> clicks(int n)
{
    std::vector<int> v;
    for (int i = 0; i < n; i++) {
        auto p = press(3, 4);
        v.insert(v.end(), p.begin(), p.end());
    }
    v.insert(v.end(), 20, 1);
    return v;
}

static int count(int gpio, button_event_type_t type)
{
    int n = 0;
    for (auto &e : s_events) {
        n += e.gpio == gpio && e.e.type == type;
    }
    return n;
}

static int events(int gpio)
{
    int n = 0;
    for (auto &e : s_events) {
        n += e.gpio == gpio;
    }
    return n;
}

// Click counts of the BUTTON_CLICKS events of a GPIO, in order.
static std::vector<int> click_counts(int gpio)
{
    std::vector<int> v;
    for (auto &e : s_events) {
        if (e.gpio == gpio && e.e.type == BUTTON_CLICKS) {
            v.push_back(e.e.clicks);
        }
    }
    return v;
}

int main()
{
    printf("button:\n");
    sim_reset(1 << 30);
    CHECK(button_init(kMain, on_button, (void *)(intptr_t)kMain) == ESP_OK);
    CHECK(button_init(kDisplay, on_button, (void *)(intptr_t)kDisplay) == ESP_OK);
    CHECK(g_sim.timers.size() == 2);
    for (const SimTimer &t : g_sim.timers) {
        CHECK(t.period_us == BUTTON_POLL_MS * 1000);
    }
    CHECK(button_init(99, on_button, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(button_init(kMain, on_button, NULL) == ESP_ERR_INVALID_STATE);
    CHECK(button_init(0, on_button, NULL) == ESP_ERR_NO_MEM);
    CHECK(g_sim.timers.size() == 2);

    // One-poll glitches are ignored, and are not clicks.
    s_events.clear();
    poll(kMain, { 1, 0, 1, 1, 0, 1, 1 });
    poll(kMain, std::vector<int>(20, 1));
    CHECK(s_events.empty());

    // Short press: held events, the first one with held_ms = 0, then one
    // release with the hold time, and once no other press follows, one click.
    // The other button stays quiet.
    s_events.clear();
    poll(kMain, press(6, 4));
    CHECK(count(kMain, BUTTON_RELEASED) == 1 && events(kDisplay) == 0);
    CHECK(!s_events.empty() && s_events.front().e.type == BUTTON_HELD && s_events.front().e.held_ms == 0);
    CHECK(!s_events.empty() && s_events.back().e.type == BUTTON_RELEASED && s_events.back().e.held_ms >= 250 &&
          s_events.back().e.held_ms <= 350);
    CHECK(click_counts(kMain).empty());     // not yet: another press may follow
    poll(kMain, std::vector<int>(10, 1));
    CHECK(click_counts(kMain) == std::vector<int>({ 1 }));

    // Same for the display button, without events from the main button.
    s_events.clear();
    poll(kDisplay, press(3, 13));
    CHECK(count(kDisplay, BUTTON_RELEASED) == 1 && events(kMain) == 0);
    CHECK(!s_events.empty() && s_events.front().e.held_ms == 0);
    CHECK(click_counts(kDisplay) == std::vector<int>({ 1 }));

    // Double press, 4 presses: one event each, with the number of presses.
    s_events.clear();
    poll(kMain, clicks(2));
    CHECK(click_counts(kMain) == std::vector<int>({ 2 }));
    s_events.clear();
    poll(kMain, clicks(4));
    CHECK(click_counts(kMain) == std::vector<int>({ 4 }));

    // Contact bounce around each press does not add presses.
    s_events.clear();
    poll(kMain, { 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1 });
    poll(kMain, std::vector<int>(20, 1));
    CHECK(click_counts(kMain) == std::vector<int>({ 2 }));

    // A press that starts as the gap ends (not debounced yet) still belongs to the series.
    s_events.clear();
    poll(kMain, press(3, 11));
    poll(kMain, press(3, 20));
    CHECK(click_counts(kMain) == std::vector<int>({ 2 }));

    // Presses 700 ms apart are separate series.
    s_events.clear();
    poll(kMain, press(3, 14));
    poll(kMain, press(3, 14));
    CHECK(click_counts(kMain) == std::vector<int>({ 1, 1 }));

    // A press of 600 ms is not a click, and it ends a series without an event.
    s_events.clear();
    poll(kMain, press(12, 20));
    CHECK(click_counts(kMain).empty() && count(kMain, BUTTON_RELEASED) == 1);
    s_events.clear();
    poll(kMain, press(3, 4));
    poll(kMain, press(12, 20));
    CHECK(click_counts(kMain).empty() && count(kMain, BUTTON_RELEASED) == 2);

    // 6 s with contact bounce at both ends: one release, about 6 s, no click.
    s_events.clear();
    std::vector<int> bouncy = { 0, 1, 0 };
    auto held = press(120, 0);
    bouncy.insert(bouncy.end(), held.begin(), held.end());
    bouncy.insert(bouncy.end(), { 1, 0, 1, 1, 1, 1 });
    bouncy.insert(bouncy.end(), 20, 1);
    poll(kMain, bouncy);
    CHECK(count(kMain, BUTTON_RELEASED) == 1 && click_counts(kMain).empty());
    for (auto &e : s_events) {
        if (e.e.type == BUTTON_RELEASED) {
            CHECK(e.e.held_ms >= 5900 && e.e.held_ms <= 6200);
        }
    }

    // Held events count up while pressed, and nothing is released while stuck.
    s_events.clear();
    poll(kMain, press(1200, 0));
    CHECK(count(kMain, BUTTON_RELEASED) == 0 && click_counts(kMain).empty());
    CHECK(!s_events.empty() && s_events.back().e.type == BUTTON_HELD && s_events.back().e.held_ms >= 59000);
    for (size_t i = 1; i < s_events.size(); i++) {
        CHECK(s_events[i].e.held_ms >= s_events[i - 1].e.held_ms);
    }

    // Both held at once: each button reports its own press.
    s_events.clear();
    g_sim.gpio_level[kMain] = 1;
    poll(kMain, { 1, 1 });
    poll(kMain, std::vector<int>(20, 1));
    s_events.clear();
    g_sim.gpio_level[kMain] = 0;
    poll(kDisplay, press(4, 2));
    CHECK(count(kDisplay, BUTTON_RELEASED) == 1 && events(kMain) > 0 && count(kMain, BUTTON_RELEASED) == 0);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
