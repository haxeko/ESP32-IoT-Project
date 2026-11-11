// See gesture_replay.h. No ESP-IDF includes: also built by test/host/run.sh.

#include "gesture_replay.h"

void GestureReplay::Init(const GestureReplayConfig &config)
{
    mCfg = config;
    Forget();
}

void GestureReplay::Forget()
{
    mKind = Kind::None;
    mCount = 0;
    mEnded = false;
    mNeedsReplay = false;
    mPending = false;
    mUnconfirmed = false;
    mEnd = 0;
    mReplayAt = kNoDeadline;
}

void GestureReplay::OnGestureStart()
{
    Forget();
}

bool GestureReplay::OnDecisive(Kind kind, uint8_t count, Delivery delivery)
{
    Forget();
    mKind = kind;
    mCount = count;
    switch (delivery) {
    case Delivery::Live:
        mUnconfirmed = true;
        return true;
    case Delivery::Priming:
        mNeedsReplay = true;
        return false;
    case Delivery::None:
        break;
    }
    mNeedsReplay = true;
    return true;
}

void GestureReplay::OnGestureEnd(int64_t now_ms, bool replayable, bool can_send)
{
    if (!replayable || mKind == Kind::None) {
        Forget();
        return;
    }
    mEnded = true;
    mEnd = now_ms;
    if (mNeedsReplay) {
        mPending = true;
        // A subscription was established while the gesture went on (long press)
        mReplayAt = can_send ? now_ms + mCfg.replay_delay_ms : kNoDeadline;
    }
}

void GestureReplay::OnReportConfirmed()
{
    mUnconfirmed = false; // delivered: whatever happens to the subscription now, the gesture is not sent again
}

void GestureReplay::OnSubscriptionEstablished(int64_t now_ms)
{
    if (mPending) {
        mReplayAt = now_ms + mCfg.replay_delay_ms;
    }
}

void GestureReplay::DelayReplay(int64_t until_ms)
{
    if (mPending && mReplayAt != kNoDeadline && mReplayAt < until_ms) {
        mReplayAt = until_ms;
    }
}

void GestureReplay::OnSubscriptionTerminated(unsigned others)
{
    if (others > 0) {
        return; // another controller is still subscribed: a replay would reach it twice
    }
    mReplayAt = kNoDeadline; // the subscription a replay was waiting for is gone
    if (mUnconfirmed) {
        // Closed before any subscription acknowledged the decisive event: refused, timed out or replaced
        mUnconfirmed = false;
        mNeedsReplay = true;
        if (mEnded) {
            mPending = true;
        }
    }
}

int64_t GestureReplay::NextDeadline() const
{
    int64_t deadline = kNoDeadline;
    if (mEnded && (mPending || mUnconfirmed)) {
        deadline = mEnd + mCfg.window_ms;
    }
    if (mPending && mReplayAt < deadline) {
        deadline = mReplayAt;
    }
    return deadline;
}

GestureReplay::Action GestureReplay::Poll(int64_t now_ms, bool can_send)
{
    if (mEnded && now_ms - mEnd >= mCfg.window_ms) {
        const bool waited = mPending;
        Forget();
        return waited ? Action::Dropped : Action::None;
    }
    if (!mPending) {
        return Action::None;
    }
    if (mReplayAt != kNoDeadline && now_ms >= mReplayAt) {
        mReplayAt = kNoDeadline;
        if (!can_send) {
            return Action::None; // gone again: wait for the next subscription
        }
        mPending = false;
        mNeedsReplay = false;
        mUnconfirmed = true; // a subscription closed before it acknowledges this report makes the gesture wait again
        return Action::Replay;
    }
    return Action::None;
}
