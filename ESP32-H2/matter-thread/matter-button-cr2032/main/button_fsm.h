#pragma once

/*
 * Button gesture state machine: debounce, single / multi press, long press and the maintenance holds
 * (pairing window, factory reset). Pure C++17 with no ESP-IDF dependency, so it runs in the host test
 * (test/host/test_button_fsm.cpp); button.cpp feeds it from the GPIO interrupt and the task timeouts.
 *
 * Time is in milliseconds from any monotonic clock. The driver calls:
 *   OnRawEdge(now)          the GPIO interrupt saw the level change (or bounce)
 *   Poll(now, pressed, out) at or after NextDeadline(), with the level sampled now
 * and gets the recognised gestures in `out`.
 *
 * Sequences (debounced), as the Matter Switch certification tests TC-SWTCH-2.5 / 2.6 expect them:
 *   single press   Press(1) ... Release(1) ... MultiDone(1)            (MultiDone after the multi-press window)
 *   double press   Press(1) Release(1) Press(2) Release(2) MultiDone(2)
 *   long press     Press(1) ... LongStart(1) ... LongRelease(1)          (no MultiDone after a long press)
 *   short + long   Press(1) Release(1) Press(2) ...... Release(2) MultiDone(2): only the first press of a
 *                  sequence can become a long press
 *   3 presses      ... Press(3) Release(3) MultiDone(3): counting goes on past multi_press_max, the Matter
 *                  layer reports such a sequence with a count of 0
 *   hold 10 s      ... LongStart, HoldPairing, (release) LongRelease(action = OpenPairing)
 *   hold 20 s      ... HoldPairing, HoldReset, (release) LongRelease(action = FactoryReset)
 *   hold > 40 s    ... HoldReset, HoldCancel, (release) LongRelease(action = None): stuck button / accident
 * With multi_press_window_ms == 0 no MultiDone is produced (single presses end at Release).
 */

#include <stddef.h>
#include <stdint.h>

struct ButtonFsmConfig {
    uint32_t debounce_ms = 20;
    uint32_t long_press_ms = 1000;
    uint32_t multi_press_window_ms = 350; // 0: multi-press detection off
    uint32_t pairing_hold_ms = 10000;     // 0: no pairing hold
    uint32_t reset_hold_ms = 20000;       // 0: no reset hold
    uint32_t stuck_hold_ms = 40000;       // a hold this long cancels the maintenance action
};

enum class ButtonEvent : uint8_t {
    Press,       // debounced press; count = position in the multi-press sequence (1 = first)
    Release,     // debounced release of a short press; count as above
    MultiDone,   // multi-press sequence finished; count = total presses (may exceed MultiPressMax)
    LongStart,   // first press of a sequence held for long_press_ms
    LongRelease, // released after LongStart; hold_ms and action are set
    HoldPairing, // still held at pairing_hold_ms (show it on the LED; acts on release)
    HoldReset,   // still held at reset_hold_ms (acts on release)
    HoldCancel,  // still held at stuck_hold_ms: maintenance action cancelled
};

enum class ButtonAction : uint8_t { None, OpenPairing, FactoryReset };

struct ButtonEventInfo {
    ButtonEvent type;
    uint8_t count;
    uint32_t hold_ms;    // Release / LongRelease: how long the button was held
    ButtonAction action; // LongRelease only
};

class ButtonFsm
{
public:
    static constexpr int64_t kNoDeadline = INT64_MAX;
    static constexpr size_t kMaxEventsPerPoll = 4;

    void Init(const ButtonFsmConfig &config, bool pressed, int64_t now_ms);

    /** Raw edge seen by the interrupt: (re)start the debounce window. */
    void OnRawEdge(int64_t now_ms);

    /** Earliest time Poll() has work to do, kNoDeadline if none. */
    int64_t NextDeadline() const;

    /** Process expired deadlines with the level sampled at now_ms. Returns the number of events written. */
    size_t Poll(int64_t now_ms, bool pressed, ButtonEventInfo (&out)[kMaxEventsPerPoll]);

    /** Debounced level (true = pressed). The driver arms the interrupt for the opposite level. */
    bool StablePressed() const { return mStablePressed; }

    /** True while a debounce window is open (the driver keeps the interrupt off meanwhile). */
    bool DebouncePending() const { return mDebounceDeadline != kNoDeadline; }

    /** No sequence in progress (no timers, button released). */
    bool Idle() const { return mState == State::Idle && !DebouncePending(); }

private:
    enum class State : uint8_t { Idle, Pressed, WaitNext, LongHeld };
    enum class Maint : uint8_t { None, Pairing, Reset, Cancelled };

    void OnPress(int64_t t, ButtonEventInfo *out, size_t &n);
    void OnRelease(int64_t t, ButtonEventInfo *out, size_t &n);
    void OnGestureDeadline(int64_t t, ButtonEventInfo *out, size_t &n);
    int64_t GestureDeadline() const;
    static void Emit(ButtonEventInfo *out, size_t &n, ButtonEvent type, uint8_t count, uint32_t hold_ms = 0,
                     ButtonAction action = ButtonAction::None);

    ButtonFsmConfig mCfg;
    State mState = State::Idle;
    Maint mMaint = Maint::None;
    bool mStablePressed = false;
    uint8_t mCount = 0;
    int64_t mPressStart = 0;
    int64_t mLastRelease = 0;
    int64_t mEdgeTime = 0;
    int64_t mDebounceDeadline = kNoDeadline;
};
