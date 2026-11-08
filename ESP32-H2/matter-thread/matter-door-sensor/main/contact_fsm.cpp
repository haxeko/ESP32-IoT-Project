// See contact_fsm.h. No ESP-IDF includes: also built by test/host/run.sh.

#include "contact_fsm.h"

void ContactFsm::Init(const ContactFsmConfig &config, bool low, int64_t now_ms)
{
    mCfg = config;
    if (mCfg.gesture_changes > kMaxGestureChanges) {
        mCfg.gesture_changes = kMaxGestureChanges;
    }
    if (mCfg.gesture_changes < 2) {
        mCfg.gesture_changes = 0;
    }
    mStableLow = low;
    mEdgeTime = now_ms;
    mDebounceDeadline = kNoDeadline;
    mChangeCount = 0;
    mChangeNext = 0;
}

void ContactFsm::OnRawEdge(int64_t now_ms)
{
    // The first edge opens the window; the level is sampled once at its end (the interrupt stays off meanwhile)
    if (mDebounceDeadline == kNoDeadline) {
        mEdgeTime = now_ms;
        mDebounceDeadline = now_ms + mCfg.debounce_ms;
    }
}

/* Returns true when this change completes a gesture */
bool ContactFsm::RecordChange(int64_t t)
{
    const uint8_t n = mCfg.gesture_changes;
    if (n == 0) {
        return false;
    }
    mChanges[mChangeNext] = t;
    mChangeNext = static_cast<uint8_t>((mChangeNext + 1) % n);
    if (mChangeCount < n) {
        mChangeCount++;
    }
    if (mChangeCount < n) {
        return false;
    }
    // The oldest of the last n changes is the next slot to be overwritten
    const int64_t oldest = mChanges[mChangeNext];
    if (t - oldest > static_cast<int64_t>(mCfg.gesture_window_ms)) {
        return false;
    }
    mChangeCount = 0;
    mChangeNext = 0;
    return true;
}

size_t ContactFsm::Poll(int64_t now_ms, bool low, ContactEventInfo (&out)[kMaxEventsPerPoll])
{
    size_t n = 0;
    if (mDebounceDeadline == kNoDeadline || now_ms < mDebounceDeadline) {
        return n;
    }
    mDebounceDeadline = kNoDeadline;
    if (low == mStableLow) {
        return n; // bounce, or a change and its reversal within the window: nothing to report
    }
    mStableLow = low;
    out[n++] = ContactEventInfo{ContactEvent::Changed, Closed()};
    if (RecordChange(mEdgeTime)) {
        out[n++] = ContactEventInfo{ContactEvent::Gesture, Closed()};
    }
    return n;
}
