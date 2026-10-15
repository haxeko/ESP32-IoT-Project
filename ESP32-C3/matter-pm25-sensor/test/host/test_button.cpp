// Host test of the polled, debounced buttons (main/drivers/button.cpp): the
// factory reset button (GPIO7) and the display button (GPIO3).
#include "sim.h"

#include "button.cpp"

static constexpr int kReset = 7;
static constexpr int kDisplay = 3;

struct Event {
    int gpio;
    uint32_t held_ms;
    bool released;
};
static std::vector<Event> s_events;

// user_data is the GPIO, to tell the buttons apart.
static void on_button(uint32_t held_ms, bool released, void *user_data)
{
    s_events.push_back({ (int)(intptr_t)user_data, held_ms, released });
}

// Runs the 50 ms poll of every button over the given levels of one GPIO (one
// per poll, 0 = pressed); the other buttons stay released.
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

static int releases(int gpio)
{
    int n = 0;
    for (auto &e : s_events) {
        n += e.gpio == gpio && e.released;
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

int main()
{
    printf("button:\n");
    sim_reset(1 << 30);
    CHECK(button_init(kReset, on_button, (void *)(intptr_t)kReset) == ESP_OK);
    CHECK(button_init(kDisplay, on_button, (void *)(intptr_t)kDisplay) == ESP_OK);
    CHECK(g_sim.timers.size() == 2);
    for (const SimTimer &t : g_sim.timers) {
        CHECK(t.period_us == BUTTON_POLL_MS * 1000);
    }
    CHECK(button_init(99, on_button, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(button_init(kReset, on_button, NULL) == ESP_ERR_INVALID_STATE);
    CHECK(button_init(5, on_button, NULL) == ESP_ERR_NO_MEM);
    CHECK(g_sim.timers.size() == 2);

    // One-poll glitches are ignored.
    s_events.clear();
    poll(kReset, { 1, 0, 1, 1, 0, 1, 1 });
    CHECK(s_events.empty());

    // Short press: held events, the first one with held_ms = 0, then one
    // release with the hold time. The other button stays quiet.
    s_events.clear();
    poll(kReset, press(6, 4));
    CHECK(releases(kReset) == 1 && events(kDisplay) == 0);
    CHECK(!s_events.empty() && !s_events.front().released && s_events.front().held_ms == 0);
    CHECK(!s_events.empty() && s_events.back().released && s_events.back().held_ms >= 250 &&
          s_events.back().held_ms <= 350);

    // Same for the display button, without events from the reset button.
    s_events.clear();
    poll(kDisplay, press(3, 3));
    CHECK(releases(kDisplay) == 1 && events(kReset) == 0);
    CHECK(!s_events.empty() && s_events.front().held_ms == 0);

    // 6 s with contact bounce at both ends: still one release, about 6 s.
    s_events.clear();
    std::vector<int> bouncy = { 0, 1, 0 };
    auto held = press(120, 0);
    bouncy.insert(bouncy.end(), held.begin(), held.end());
    bouncy.insert(bouncy.end(), { 1, 0, 1, 1, 1, 1 });
    poll(kReset, bouncy);
    CHECK(releases(kReset) == 1);
    CHECK(s_events.back().released && s_events.back().held_ms >= 5900 && s_events.back().held_ms <= 6200);

    // Held events count up while pressed, and nothing is released while stuck.
    s_events.clear();
    poll(kReset, press(1200, 0));
    CHECK(releases(kReset) == 0);
    CHECK(!s_events.empty() && s_events.back().held_ms >= 59000);
    for (size_t i = 1; i < s_events.size(); i++) {
        CHECK(s_events[i].held_ms >= s_events[i - 1].held_ms);
    }

    // Both held at once: each button reports its own press.
    s_events.clear();
    g_sim.gpio_level[kReset] = 1;
    poll(kReset, { 1, 1 });
    s_events.clear();
    g_sim.gpio_level[kReset] = 0;
    poll(kDisplay, press(4, 2));
    CHECK(releases(kDisplay) == 1 && events(kReset) > 0 && releases(kReset) == 0);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
