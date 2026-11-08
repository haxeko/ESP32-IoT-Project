#pragma once

/*
 * Door contact state machine: a reed switch on the button input (in parallel with SW1, or in its place). Debounce,
 * the reported contact state, and the magnet gesture that replaces the button's maintenance holds. Pure C++17 with
 * no ESP-IDF dependency, so it runs in the host test (test/host/test_contact_fsm.cpp); contact.cpp feeds it from the
 * GPIO interrupt and the task timeouts.
 *
 * Time is in milliseconds from any monotonic clock. The driver calls:
 *   OnRawEdge(now)       the GPIO interrupt saw the level change (or bounce)
 *   Poll(now, low, out)  at or after NextDeadline(), with the level sampled now
 * and gets the events in `out`:
 *   Changed(closed)      debounced contact change (closed = magnet at the reed = door closed)
 *   Gesture              `gesture_changes` changes within `gesture_window_ms`, after the change that completes them:
 *                        a magnet moved away and back again and again, which a door does not do. The change
 *                        history is then cleared, so a second gesture needs as many changes again.
 *
 * Polarity: a normally open reed (the common kind) closes when the magnet is near, pulling the input low, so
 * "closed" = low (closed_when_low). A normally closed reed opens at the magnet: closed_when_low = false.
 */

#include <stddef.h>
#include <stdint.h>

struct ContactFsmConfig {
    uint32_t debounce_ms = 50;
    bool closed_when_low = true;
    uint8_t gesture_changes = 10; // 0 or 1: no gesture; at most ContactFsm::kMaxGestureChanges
    uint32_t gesture_window_ms = 10000;
};

enum class ContactEvent : uint8_t {
    Changed, // `closed` is the new state
    Gesture, // `closed` is the state after the change that completed the gesture
};

struct ContactEventInfo {
    ContactEvent type;
    bool closed;
};

class ContactFsm
{
public:
    static constexpr int64_t kNoDeadline = INT64_MAX;
    static constexpr size_t kMaxEventsPerPoll = 2;
    static constexpr uint8_t kMaxGestureChanges = 16;

    void Init(const ContactFsmConfig &config, bool low, int64_t now_ms);

    /** Raw edge seen by the interrupt: open the debounce window (the level is sampled once at its end). */
    void OnRawEdge(int64_t now_ms);

    /** Earliest time Poll() has work to do, kNoDeadline if none. */
    int64_t NextDeadline() const { return mDebounceDeadline; }

    /** Process an expired debounce window with the level sampled at now_ms. Returns the number of events written. */
    size_t Poll(int64_t now_ms, bool low, ContactEventInfo (&out)[kMaxEventsPerPoll]);

    /** Debounced level (true = low). The driver arms the interrupt for the opposite level. */
    bool StableLow() const { return mStableLow; }

    /** Debounced contact state (true = closed). */
    bool Closed() const { return mStableLow == mCfg.closed_when_low; }

    /** True while a debounce window is open (the driver keeps the interrupt off meanwhile). */
    bool DebouncePending() const { return mDebounceDeadline != kNoDeadline; }

private:
    bool RecordChange(int64_t t);

    ContactFsmConfig mCfg;
    bool mStableLow = false;
    int64_t mEdgeTime = 0;
    int64_t mDebounceDeadline = kNoDeadline;
    // Times of the last changes (ring buffer), for the gesture
    int64_t mChanges[kMaxGestureChanges] = {};
    uint8_t mChangeCount = 0;
    uint8_t mChangeNext = 0;
};
