// Host test of the polled, debounced button (main/drivers/button.cpp).
#include "sim.h"

#include "button.cpp"

struct Event {
    uint32_t held_ms;
    bool released;
};
static std::vector<Event> s_events;

static void on_button(uint32_t held_ms, bool released, void *)
{
    s_events.push_back({ held_ms, released });
}

// Runs the 50 ms poll over the given levels (one per poll, 0 = pressed).
static void poll(const std::vector<int> &levels)
{
    for (int level : levels) {
        g_sim.gpio_level = level;
        g_sim.now_us += BUTTON_POLL_MS * 1000;
        g_sim.timer_cb(NULL);
    }
}

static std::vector<int> press(int pressed_polls, int released_polls)
{
    std::vector<int> v(pressed_polls, 0);
    v.insert(v.end(), released_polls, 1);
    return v;
}

static int releases()
{
    int n = 0;
    for (auto &e : s_events) {
        n += e.released;
    }
    return n;
}

int main()
{
    printf("button:\n");
    sim_reset(1 << 30);
    CHECK(button_init(7, on_button, NULL) == ESP_OK);
    CHECK(g_sim.timer_cb != nullptr && g_sim.timer_period_us == BUTTON_POLL_MS * 1000);
    CHECK(button_init(99, on_button, NULL) == ESP_ERR_INVALID_ARG);

    // One-poll glitches are ignored.
    s_events.clear();
    poll({ 1, 0, 1, 1, 0, 1, 1 });
    CHECK(s_events.empty());

    // Short press: held events, then one release with the hold time.
    s_events.clear();
    poll(press(6, 4));
    CHECK(releases() == 1);
    CHECK(!s_events.empty() && s_events.back().released && s_events.back().held_ms >= 250 &&
          s_events.back().held_ms <= 350);

    // 6 s with contact bounce at both ends: still one release, about 6 s.
    s_events.clear();
    std::vector<int> bouncy = { 0, 1, 0 };
    auto held = press(120, 0);
    bouncy.insert(bouncy.end(), held.begin(), held.end());
    bouncy.insert(bouncy.end(), { 1, 0, 1, 1, 1, 1 });
    poll(bouncy);
    CHECK(releases() == 1);
    CHECK(s_events.back().released && s_events.back().held_ms >= 5900 && s_events.back().held_ms <= 6200);

    // Held events count up while pressed, and nothing is released while stuck.
    s_events.clear();
    poll(press(1200, 0));
    CHECK(releases() == 0);
    CHECK(!s_events.empty() && s_events.back().held_ms >= 59000);
    for (size_t i = 1; i < s_events.size(); i++) {
        CHECK(s_events[i].held_ms >= s_events[i - 1].held_ms);
    }

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
