// See button_fsm.h. No ESP-IDF includes: also built by test/host/run.sh.

#include "button_fsm.h"

void ButtonFsm::Init(const ButtonFsmConfig &config, bool pressed, int64_t now_ms)
{
    mCfg = config;
    // The maintenance holds only make sense after the long press, each after the previous one
    if (mCfg.pairing_hold_ms && mCfg.pairing_hold_ms <= mCfg.long_press_ms) {
        mCfg.pairing_hold_ms = 0;
    }
    if (mCfg.reset_hold_ms && mCfg.reset_hold_ms <= mCfg.long_press_ms) {
        mCfg.reset_hold_ms = 0;
    }
    if (mCfg.reset_hold_ms && mCfg.reset_hold_ms <= mCfg.pairing_hold_ms) {
        mCfg.pairing_hold_ms = 0;
    }
    const uint32_t last_hold = mCfg.reset_hold_ms ? mCfg.reset_hold_ms : mCfg.pairing_hold_ms;
    if (mCfg.stuck_hold_ms && mCfg.stuck_hold_ms <= last_hold) {
        mCfg.stuck_hold_ms = 0;
    }

    // Held at start (e.g. boot-time factory reset gesture): no Press now, and its release produces nothing
    mStablePressed = pressed;
    mState = State::Idle;
    mMaint = Maint::None;
    mCount = 0;
    mPressStart = now_ms;
    mLastRelease = now_ms;
    mEdgeTime = now_ms;
    mDebounceDeadline = kNoDeadline;
}

void ButtonFsm::OnRawEdge(int64_t now_ms)
{
    // The first edge opens the window; the level is sampled once at its end (the interrupt stays off meanwhile)
    if (mDebounceDeadline == kNoDeadline) {
        mEdgeTime = now_ms;
        mDebounceDeadline = now_ms + mCfg.debounce_ms;
    }
}

int64_t ButtonFsm::GestureDeadline() const
{
    switch (mState) {
    case State::Pressed:
        // Later presses of a multi-press sequence never become long presses (TC-SWTCH-2.5 step 8a)
        return mCount == 1 ? mPressStart + mCfg.long_press_ms : kNoDeadline;
    case State::WaitNext:
        return mLastRelease + mCfg.multi_press_window_ms;
    case State::LongHeld:
        switch (mMaint) {
        case Maint::None:
            if (mCfg.pairing_hold_ms) {
                return mPressStart + mCfg.pairing_hold_ms;
            }
            if (mCfg.reset_hold_ms) {
                return mPressStart + mCfg.reset_hold_ms;
            }
            return kNoDeadline;
        case Maint::Pairing:
            if (mCfg.reset_hold_ms) {
                return mPressStart + mCfg.reset_hold_ms;
            }
            return mCfg.stuck_hold_ms ? mPressStart + mCfg.stuck_hold_ms : kNoDeadline;
        case Maint::Reset:
            return mCfg.stuck_hold_ms ? mPressStart + mCfg.stuck_hold_ms : kNoDeadline;
        case Maint::Cancelled:
            return kNoDeadline;
        }
        return kNoDeadline;
    case State::Idle:
        break;
    }
    return kNoDeadline;
}

int64_t ButtonFsm::NextDeadline() const
{
    int64_t gesture = GestureDeadline();
    // An edge seen before the gesture deadline may be a real transition that happened first: the deadline
    // waits for the debounce verdict (e.g. a release 1 ms before the long press threshold)
    if (mDebounceDeadline != kNoDeadline && mEdgeTime < gesture) {
        gesture = kNoDeadline;
    }
    return gesture < mDebounceDeadline ? gesture : mDebounceDeadline;
}

void ButtonFsm::Emit(ButtonEventInfo *out, size_t &n, ButtonEvent type, uint8_t count, uint32_t hold_ms,
                     ButtonAction action)
{
    out[n++] = ButtonEventInfo{type, count, hold_ms, action};
}

void ButtonFsm::OnPress(int64_t t, ButtonEventInfo *out, size_t &n)
{
    switch (mState) {
    case State::Idle:
        mCount = 1;
        break;
    case State::WaitNext:
        if (mCount < UINT8_MAX) {
            mCount++;
        }
        break;
    case State::Pressed:
    case State::LongHeld:
        return; // cannot happen: the stable level was already "pressed"
    }
    mPressStart = t;
    mMaint = Maint::None;
    mState = State::Pressed;
    Emit(out, n, ButtonEvent::Press, mCount);
}

void ButtonFsm::OnRelease(int64_t t, ButtonEventInfo *out, size_t &n)
{
    const uint32_t hold = t > mPressStart ? static_cast<uint32_t>(t - mPressStart) : 0;
    switch (mState) {
    case State::Pressed:
        Emit(out, n, ButtonEvent::Release, mCount, hold);
        if (mCfg.multi_press_window_ms == 0) {
            mState = State::Idle;
        } else {
            mLastRelease = t;
            mState = State::WaitNext;
        }
        break;
    case State::LongHeld: {
        ButtonAction action = ButtonAction::None;
        if (mMaint == Maint::Pairing) {
            action = ButtonAction::OpenPairing;
        } else if (mMaint == Maint::Reset) {
            action = ButtonAction::FactoryReset;
        }
        Emit(out, n, ButtonEvent::LongRelease, mCount, hold, action);
        mMaint = Maint::None;
        mState = State::Idle;
        break;
    }
    case State::Idle:     // release of a press that started before Init (held at boot)
    case State::WaitNext: // cannot happen: the stable level was already "released"
        break;
    }
}

void ButtonFsm::OnGestureDeadline(int64_t t, ButtonEventInfo *out, size_t &n)
{
    (void)t;
    switch (mState) {
    case State::Pressed:
        mState = State::LongHeld;
        mMaint = Maint::None;
        Emit(out, n, ButtonEvent::LongStart, mCount);
        break;
    case State::WaitNext:
        mState = State::Idle;
        Emit(out, n, ButtonEvent::MultiDone, mCount);
        break;
    case State::LongHeld:
        switch (mMaint) {
        case Maint::None:
            if (mCfg.pairing_hold_ms) {
                mMaint = Maint::Pairing;
                Emit(out, n, ButtonEvent::HoldPairing, mCount);
            } else {
                mMaint = Maint::Reset;
                Emit(out, n, ButtonEvent::HoldReset, mCount);
            }
            break;
        case Maint::Pairing:
            if (mCfg.reset_hold_ms) {
                mMaint = Maint::Reset;
                Emit(out, n, ButtonEvent::HoldReset, mCount);
            } else {
                mMaint = Maint::Cancelled;
                Emit(out, n, ButtonEvent::HoldCancel, mCount);
            }
            break;
        case Maint::Reset:
            mMaint = Maint::Cancelled;
            Emit(out, n, ButtonEvent::HoldCancel, mCount);
            break;
        case Maint::Cancelled:
            break;
        }
        break;
    case State::Idle:
        break;
    }
}

size_t ButtonFsm::Poll(int64_t now_ms, bool pressed, ButtonEventInfo (&out)[kMaxEventsPerPoll])
{
    size_t n = 0;
    // Each step emits at most one event; leave the rest for the next call
    while (n < kMaxEventsPerPoll) {
        const int64_t gesture = GestureDeadline();
        const bool commit = mDebounceDeadline != kNoDeadline && now_ms >= mDebounceDeadline;
        // Handle what happened first: a long press deadline before the (late polled) release edge counts first
        if (commit && (gesture == kNoDeadline || mEdgeTime < gesture)) {
            mDebounceDeadline = kNoDeadline;
            if (pressed != mStablePressed) {
                mStablePressed = pressed;
                if (pressed) {
                    OnPress(mEdgeTime, out, n);
                } else {
                    OnRelease(mEdgeTime, out, n);
                }
            }
            continue;
        }
        if (gesture != kNoDeadline && gesture <= now_ms) {
            if (mDebounceDeadline != kNoDeadline && mEdgeTime < gesture) {
                break; // the pending edge came first: wait for its debounce verdict (see NextDeadline)
            }
            OnGestureDeadline(gesture, out, n);
            continue;
        }
        break;
    }
    return n;
}
