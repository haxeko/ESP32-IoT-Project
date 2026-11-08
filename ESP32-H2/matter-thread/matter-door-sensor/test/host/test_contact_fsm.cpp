// Host test of the door contact state machine (main/contact_fsm.cpp): debounce, polarity, the magnet gesture.
#include "contact_fsm.h"

#include <cstdio>
#include <vector>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

struct Rec {
    ContactEvent type;
    bool closed;
};

// One debounced edge: the interrupt at t, the level sampled at the end of the window
static std::vector<Rec> edge(ContactFsm &f, int64_t t, bool low, uint32_t debounce = 50)
{
    f.OnRawEdge(t);
    ContactEventInfo out[ContactFsm::kMaxEventsPerPoll];
    const size_t n = f.Poll(t + debounce, low, out);
    std::vector<Rec> r;
    for (size_t i = 0; i < n; i++) {
        r.push_back({out[i].type, out[i].closed});
    }
    return r;
}

static bool is_change(const std::vector<Rec> &r, bool closed)
{
    return r.size() == 1 && r[0].type == ContactEvent::Changed && r[0].closed == closed;
}

int main()
{
    ContactFsmConfig cfg; // NO reed: closed = low, 10 changes in 10 s
    {
        ContactFsm f;
        f.Init(cfg, true, 0);
        check(f.Closed() && f.StableLow() && !f.DebouncePending(), "start low: closed, nothing pending");
        check(is_change(edge(f, 1000, false), false), "reed opens: Changed(open)");
        check(is_change(edge(f, 5000, true), true), "reed closes: Changed(closed)");
    }
    {
        ContactFsm f;
        f.Init(cfg, true, 0);
        f.OnRawEdge(1000);
        ContactEventInfo out[ContactFsm::kMaxEventsPerPoll];
        check(f.Poll(1020, false, out) == 0 && f.DebouncePending(), "before the window ends: nothing");
        check(f.NextDeadline() == 1050, "deadline at the end of the window");
        check(f.Poll(1050, true, out) == 0 && !f.DebouncePending() && f.Closed(), "bounce back: nothing reported");
    }
    {
        ContactFsmConfig nc = cfg;
        nc.closed_when_low = false; // NC reed: the magnet opens it, door closed = high
        ContactFsm f;
        f.Init(nc, false, 0);
        check(f.Closed(), "NC reed, high at start: closed");
        check(is_change(edge(f, 1000, true), false), "NC reed pulled low: door open");
    }
    {
        // Door used by hand: a change every 2 s never makes a gesture
        ContactFsm f;
        f.Init(cfg, true, 0);
        bool gesture = false;
        bool low = true;
        for (int i = 1; i <= 30; i++) {
            low = !low;
            for (const Rec &r : edge(f, i * 2000, low)) {
                gesture |= r.type == ContactEvent::Gesture;
            }
        }
        check(!gesture, "a change every 2 s (30 changes): no gesture");
    }
    {
        // Magnet moved away and back 5 times in ~4.5 s: 10 changes
        ContactFsm f;
        f.Init(cfg, true, 0);
        bool low = true;
        std::vector<Rec> last;
        int gestures = 0;
        for (int i = 1; i <= 10; i++) {
            low = !low;
            last = edge(f, 10000 + i * 450, low);
            for (const Rec &r : last) {
                gestures += r.type == ContactEvent::Gesture;
            }
        }
        check(gestures == 1 && last.size() == 2 && last[0].type == ContactEvent::Changed &&
                  last[1].type == ContactEvent::Gesture && last[1].closed,
              "10 changes in 4.5 s: Changed then Gesture, after the 10th change");
        // Two more changes right after: the history was cleared, no second gesture
        low = !low;
        check(is_change(edge(f, 15000, low), false), "next change after a gesture: Changed only");
    }
    {
        // 10 changes spread over 12 s: too slow
        ContactFsm f;
        f.Init(cfg, true, 0);
        bool low = true;
        int gestures = 0;
        for (int i = 0; i < 10; i++) {
            low = !low;
            for (const Rec &r : edge(f, 20000 + i * 1300, low)) {
                gestures += r.type == ContactEvent::Gesture;
            }
        }
        check(gestures == 0, "10 changes over 11.7 s: no gesture");
        // ...but the window slides: 9 earlier changes no longer count, a fast burst still works
        for (int i = 0; i < 10; i++) {
            low = !low;
            for (const Rec &r : edge(f, 40000 + i * 300, low)) {
                gestures += r.type == ContactEvent::Gesture;
            }
        }
        check(gestures == 1, "then 10 fast changes: one gesture");
    }
    {
        ContactFsmConfig off = cfg;
        off.gesture_changes = 0;
        ContactFsm f;
        f.Init(off, true, 0);
        bool low = true;
        int gestures = 0;
        for (int i = 1; i <= 20; i++) {
            low = !low;
            for (const Rec &r : edge(f, i * 100, low)) {
                gestures += r.type == ContactEvent::Gesture;
            }
        }
        check(gestures == 0, "gesture_changes 0: off");
    }
    {
        ContactFsmConfig big = cfg;
        big.gesture_changes = 200; // clamped to kMaxGestureChanges
        big.gesture_window_ms = 30000;
        ContactFsm f;
        f.Init(big, true, 0);
        bool low = true;
        int at = 0;
        for (int i = 1; i <= 20 && at == 0; i++) {
            low = !low;
            for (const Rec &r : edge(f, i * 200, low)) {
                if (r.type == ContactEvent::Gesture) {
                    at = i;
                }
            }
        }
        check(at == ContactFsm::kMaxGestureChanges, "gesture_changes above the maximum: clamped to 16");
    }

    std::printf("contact FSM: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
