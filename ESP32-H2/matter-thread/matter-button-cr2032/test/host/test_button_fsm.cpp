// Host test of the gesture state machine (main/button_fsm.cpp), driven like main/button.cpp does on the chip:
// a level interrupt armed for the opposite of the debounced level, switched off during the debounce window,
// and Poll() at NextDeadline() (or later, to model a busy CPU).
#include "button_fsm.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

struct Got {
    int64_t t;
    ButtonEvent type;
    int count;
    uint32_t hold;
    ButtonAction action;
};

// level: true = pressed. changes: (time ms, new raw level). poll_step: 0 = poll exactly at the deadline,
// N = only every N ms (late polling).
static std::vector<Got> run(const ButtonFsmConfig &cfg, bool initial, std::vector<std::pair<int64_t, bool>> changes,
                            int64_t end_ms, int64_t poll_step = 0)
{
    ButtonFsm fsm;
    fsm.Init(cfg, initial, 0);
    std::vector<Got> got;
    bool raw = initial;
    size_t next = 0;
    for (int64_t now = 0; now <= end_ms; now++) {
        while (next < changes.size() && changes[next].first == now) {
            raw = changes[next].second;
            next++;
        }
        // Interrupt: armed (not debouncing) for the level opposite to the stable one
        if (!fsm.DebouncePending() && raw != fsm.StablePressed()) {
            fsm.OnRawEdge(now);
        }
        const bool due = fsm.NextDeadline() <= now;
        if (due && (poll_step == 0 || now % poll_step == 0)) {
            ButtonEventInfo ev[ButtonFsm::kMaxEventsPerPoll];
            size_t n;
            do {
                n = fsm.Poll(now, raw, ev);
                for (size_t i = 0; i < n; i++) {
                    got.push_back({now, ev[i].type, ev[i].count, ev[i].hold_ms, ev[i].action});
                }
            } while (n > 0 && fsm.NextDeadline() <= now);
        }
    }
    return got;
}

static const char *name(ButtonEvent e)
{
    switch (e) {
    case ButtonEvent::Press: return "P";
    case ButtonEvent::Release: return "R";
    case ButtonEvent::MultiDone: return "M";
    case ButtonEvent::LongStart: return "L";
    case ButtonEvent::LongRelease: return "LR";
    case ButtonEvent::HoldPairing: return "HP";
    case ButtonEvent::HoldReset: return "HR";
    case ButtonEvent::HoldCancel: return "HC";
    }
    return "?";
}

static std::string describe(const std::vector<Got> &got)
{
    std::string s;
    for (const Got &g : got) {
        if (!s.empty()) {
            s += ' ';
        }
        s += name(g.type) + std::to_string(g.count);
        if (g.type == ButtonEvent::LongRelease) {
            s += g.action == ButtonAction::OpenPairing ? "/pair" : g.action == ButtonAction::FactoryReset ? "/reset" : "";
        }
    }
    return s;
}

static int s_failures = 0;

static void expect(const char *test, const std::vector<Got> &got, const std::string &want)
{
    const std::string have = describe(got);
    if (have != want) {
        std::printf("FAIL %s\n  want: %s\n  have: %s\n", test, want.c_str(), have.c_str());
        s_failures++;
    } else {
        std::printf("ok   %s: %s\n", test, have.c_str());
    }
}

static void expect_true(const char *test, bool cond)
{
    if (!cond) {
        std::printf("FAIL %s\n", test);
        s_failures++;
    }
}

// A press from `at` lasting `ms`, with 3 ms of contact bounce on both edges
static void press(std::vector<std::pair<int64_t, bool>> &v, int64_t at, int64_t ms)
{
    v.push_back({at, true});
    v.push_back({at + 1, false});
    v.push_back({at + 2, true});
    v.push_back({at + ms, false});
    v.push_back({at + ms + 1, true});
    v.push_back({at + ms + 3, false});
}

int main()
{
    ButtonFsmConfig cfg; // defaults: debounce 20, long 1000, window 350, holds 10/20/40 s

    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 120);
        auto got = run(cfg, false, v, 2000);
        expect("single press", got, "P1 R1 M1");
        // Press reported after the debounce window, MultiDone after the multi-press window
        expect_true("single press: Press at +20 ms", got.size() == 3 && got[0].t == 120);
        expect_true("single press: hold ~120 ms", got.size() == 3 && got[1].hold == 120);
        expect_true("single press: MultiDone 350 ms after release", got.size() == 3 && got[2].t == 220 + 350);
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 100);
        press(v, 350, 100);
        auto got = run(cfg, false, v, 2000);
        expect("double press", got, "P1 R1 P2 R2 M2");
        expect_true("double press: MultiDone 350 ms after the 2nd release", got.size() == 5 && got[4].t == 450 + 350);
    }
    {
        // The Matter layer turns a count above MultiPressMax into 0 (TC-SWTCH-2.6 step 6)
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 80);
        press(v, 300, 80);
        press(v, 500, 80);
        expect("triple press keeps counting", run(cfg, false, v, 2000), "P1 R1 P2 R2 P3 R3 M3");
    }
    {
        // Gap longer than the window: two separate single presses
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 80);
        press(v, 600, 80);
        expect("two presses 420 ms apart", run(cfg, false, v, 2000), "P1 R1 M1 P1 R1 M1");
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 1500);
        auto got = run(cfg, false, v, 3000);
        expect("long press", got, "P1 L1 LR1");
        expect_true("long press: LongStart at 1 s", got.size() == 3 && got[1].t == 100 + 1000);
        expect_true("long press: hold 1500 ms", got.size() == 3 && got[2].hold == 1500);
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 999);
        expect("999 ms is still a short press", run(cfg, false, v, 3000), "P1 R1 M1");
    }
    {
        // Release at 1005 ms, but the CPU only polls every 30 ms: the long press deadline (1000 ms) came first
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 0, 1005);
        expect("late poll keeps the event order", run(cfg, false, v, 3000, 30), "P1 L1 LR1");
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 12000);
        expect("hold 12 s: pairing", run(cfg, false, v, 15000), "P1 L1 HP1 LR1/pair");
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 25000);
        expect("hold 25 s: factory reset", run(cfg, false, v, 30000), "P1 L1 HP1 HR1 LR1/reset");
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 45000);
        expect("hold 45 s: stuck, cancelled", run(cfg, false, v, 50000), "P1 L1 HP1 HR1 HC1 LR1");
    }
    {
        std::vector<std::pair<int64_t, bool>> v = {{100, true}, {105, false}};
        expect("5 ms glitch ignored", run(cfg, false, v, 1000), "");
    }
    {
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 100);
        press(v, 350, 1500);
        // Only the first press of a sequence can be a long press (TC-SWTCH-2.5 / 2.6 step 8a)
        expect("short then long", run(cfg, false, v, 3000), "P1 R1 P2 R2 M2");
    }
    {
        // Long press then a short one: LongRelease ends the first sequence (TC-SWTCH-2.5 / 2.6 step 9a)
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 1500);
        press(v, 1700, 100);
        expect("long then short", run(cfg, false, v, 3000), "P1 L1 LR1 P1 R1 M1");
    }
    {
        std::vector<std::pair<int64_t, bool>> v = {{500, false}};
        expect("held at boot, released: nothing", run(cfg, true, v, 2000), "");
    }
    {
        ButtonFsmConfig c = cfg;
        c.multi_press_window_ms = 0;
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 100);
        expect("multi-press off", run(c, false, v, 2000), "P1 R1");
    }
    {
        ButtonFsmConfig c = cfg;
        c.pairing_hold_ms = 0;
        std::vector<std::pair<int64_t, bool>> v;
        press(v, 100, 22000);
        expect("no pairing hold: reset hold only", run(c, false, v, 30000), "P1 L1 HR1 LR1/reset");
    }
    {
        // Bounce long enough to be sampled mid-bounce: the level read at the end of the window decides,
        // and the re-armed interrupt catches the real final level
        std::vector<std::pair<int64_t, bool>> v = {{100, true}, {118, false}, {119, true}, {121, false}, {122, true},
                                                   {300, false}};
        expect("bounce across the debounce window", run(cfg, false, v, 2000), "P1 R1 M1");
    }

    if (s_failures) {
        std::printf("%d button FSM test(s) FAILED\n", s_failures);
        return 1;
    }
    std::printf("button FSM: all tests passed\n");
    return 0;
}
