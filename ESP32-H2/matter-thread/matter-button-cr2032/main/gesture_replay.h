#pragma once

/*
 * Sends a gesture again when no controller could receive it. Pure C++17 with no ESP-IDF or Matter dependency, so it
 * runs in the host test (test/host/test_gesture_replay.cpp); switch_events.cpp feeds it and generates the events.
 *
 * Why: Switch events reach only the subscriptions established when they are generated. With none (Thread detached or
 * paused, the controller dropped its subscription, a keep-alive report was lost) the events only stay in the event
 * log. The controller reads them in the priming report of its next subscription, and Apple's Matter.framework marks
 * the events of a priming report as historical (MTRDevice, MTREventIsHistoricalKey): no automation runs. The first
 * press after an outage then only brings the controller back (it subscribes again at once on a report for a
 * subscription it no longer has) and the second press is the one that works.
 *
 * What decides is the "decisive" event of a gesture, the one controllers act on: MultiPressComplete for a single or
 * double press, LongPress for a long press. When it is about to be generated, `Delivery` says who can receive it:
 *   Live     a subscription is established: generate it now, and watch its report. The gesture is delivered once a
 *            subscription has acknowledged the report that carries the event (OnReportConfirmed). If the last
 *            subscription is closed before that (the controller refused the report with InvalidSubscription, the
 *            report timed out, the controller replaced the subscription), the gesture waits.
 *   Priming  a subscription is being set up: generated now, the event could reach the controller either inside the
 *            priming report (historical, ignored) or right after it (live). It is held back instead, and the gesture
 *            waits.
 *   None     nobody listens: generate it now (it will be historical), and the gesture waits.
 * A waiting gesture is generated again, as new events, replay_delay_ms after a subscription is established (later if
 * the caller asks, DelayReplay), if that happens within window_ms of the end of the gesture; then it is dropped. A gesture a controller acknowledged is
 * never sent again, and a newer gesture replaces a waiting one: one action, not two.
 *
 * Time is in milliseconds from any monotonic clock.
 */

#include <stdint.h>

struct GestureReplayConfig {
    uint32_t window_ms = 30000;     // a gesture is forgotten this long after it ended
    uint32_t replay_delay_ms = 400; // after "established": the SubscribeResponse leaves first
};

class GestureReplay
{
public:
    enum class Kind : uint8_t { None, Presses, Long };
    enum class Delivery : uint8_t { None, Priming, Live };
    enum class Action : uint8_t { None, Replay, Dropped };

    static constexpr int64_t kNoDeadline = INT64_MAX;

    void Init(const GestureReplayConfig &config);

    /** First press of a gesture: a waiting gesture is forgotten (replaced by this one). */
    void OnGestureStart();

    /** The decisive event is about to be generated. Returns true to generate it now, false to hold it back. */
    bool OnDecisive(Kind kind, uint8_t count, Delivery delivery);

    /** The gesture is over (after OnDecisive). replayable = false: never sent again (maintenance hold, aborted
     *  sequence). can_send: a subscription is established and the network is up right now. */
    void OnGestureEnd(int64_t now_ms, bool replayable, bool can_send);

    /** While AwaitingConfirm(): an established subscription acknowledged the report with the decisive event. */
    void OnReportConfirmed();

    void OnSubscriptionEstablished(int64_t now_ms);
    /** A replay that is due waits until until_ms (the controller has not acknowledged the SubscribeResponse yet). */
    void DelayReplay(int64_t until_ms);
    /** others: subscriptions still established after this one. */
    void OnSubscriptionTerminated(unsigned others);

    /** Earliest time Poll() has work to do, kNoDeadline if none. */
    int64_t NextDeadline() const;

    /** At or after NextDeadline(). Replay: generate the events of Kind() / Count() now. One action per call. */
    Action Poll(int64_t now_ms, bool can_send);

    /** A gesture waits for a subscription. */
    bool Pending() const { return mPending; }
    /** The decisive event (first time or sent again) went to an established subscription, not acknowledged yet. */
    bool AwaitingConfirm() const { return mUnconfirmed; }
    Kind GetKind() const { return mKind; }
    uint8_t GetCount() const { return mCount; }
    /** When the gesture ended (valid while Pending() and in the Poll() call that returns Replay). */
    int64_t GestureEnd() const { return mEnd; }

private:
    void Forget();

    GestureReplayConfig mCfg;
    Kind mKind = Kind::None;
    uint8_t mCount = 0;
    bool mEnded = false;       // OnGestureEnd() seen
    bool mNeedsReplay = false; // decided at the decisive event or by a closed subscription; mPending at the end
    bool mPending = false;
    bool mUnconfirmed = false;
    int64_t mEnd = 0;
    int64_t mReplayAt = kNoDeadline;
};
