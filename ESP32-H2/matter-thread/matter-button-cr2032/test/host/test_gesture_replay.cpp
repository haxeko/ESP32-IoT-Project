// Host test of the gesture replay logic (main/gesture_replay.cpp): which gesture waits, when it is sent again, and
// that no gesture a controller acknowledged is sent again.
#include "gesture_replay.h"

#include <cstdio>

static int s_failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        s_failures++;
    }
}

using Kind = GestureReplay::Kind;
using Delivery = GestureReplay::Delivery;
using Action = GestureReplay::Action;

static GestureReplay make()
{
    GestureReplay r;
    r.Init(GestureReplayConfig()); // window 30 s, replay delay 400 ms
    return r;
}

// A single press whose decisive event comes at `t`
static bool press(GestureReplay &r, int64_t t, Delivery d, uint8_t count = 1)
{
    r.OnGestureStart();
    const bool now = r.OnDecisive(Kind::Presses, count, d);
    r.OnGestureEnd(t, true, d == Delivery::Live);
    return now;
}

int main()
{
    {
        GestureReplay r = make();
        check(press(r, 1000, Delivery::Live) && !r.Pending() && r.AwaitingConfirm(),
              "live: generated now, nothing waits, the report is watched");
        r.OnReportConfirmed();
        check(!r.AwaitingConfirm() && r.NextDeadline() == GestureReplay::kNoDeadline, "acknowledged: delivered");
        r.OnSubscriptionTerminated(0);
        r.OnSubscriptionEstablished(2000);
        check(!r.Pending() && r.Poll(2400, true) == Action::None,
              "acknowledged, then the controller subscribes again: no second action");
        check(r.Poll(60000, true) == Action::None, "live: nothing later");
    }
    {
        // Thread paused or no subscription: the first press after an outage
        GestureReplay r = make();
        check(press(r, 1000, Delivery::None, 2) && r.Pending() && !r.AwaitingConfirm(),
              "nobody listens: generated now, and waits");
        check(r.NextDeadline() == 31000, "waits until the end of the window");
        r.OnSubscriptionEstablished(4000);
        check(r.NextDeadline() == 4400 && r.Poll(4399, true) == Action::None, "replay 400 ms after established");
        check(r.Poll(4400, true) == Action::Replay && r.GetKind() == Kind::Presses && r.GetCount() == 2 &&
                  r.GestureEnd() == 1000 && !r.Pending() && r.AwaitingConfirm(),
              "sent again: double press, once, and its report is watched");
        r.OnReportConfirmed();
        check(r.Poll(5000, true) == Action::None && r.NextDeadline() == GestureReplay::kNoDeadline,
              "nothing after the replay");
        r.OnSubscriptionEstablished(6000);
        check(r.Poll(6400, true) == Action::None, "a second controller subscribing later: no second replay");
    }
    {
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        check(r.Poll(30999, false) == Action::None && r.Poll(31000, false) == Action::Dropped && !r.Pending(),
              "no subscription within the window: dropped");
        r.OnSubscriptionEstablished(32000);
        check(r.Poll(32400, true) == Action::None, "a subscription after the window: nothing");
    }
    {
        // A subscription is being primed: the decisive event is held back, then generated once, live
        GestureReplay r = make();
        check(!press(r, 1000, Delivery::Priming) && r.Pending(), "priming: decisive event held back");
        r.OnSubscriptionEstablished(2200);
        check(r.Poll(2600, true) == Action::Replay, "priming: generated after established");
    }
    {
        // Stale subscription: the controller refuses the report (InvalidSubscription) and subscribes again at once
        GestureReplay r = make();
        check(press(r, 1000, Delivery::Live) && !r.Pending(), "stale subscription: looks live");
        r.OnSubscriptionTerminated(0);
        check(r.Pending() && !r.AwaitingConfirm() && r.NextDeadline() == 31000,
              "closed before the report was acknowledged: the gesture waits");
        r.OnSubscriptionEstablished(1320); // however soon the new subscription comes
        check(r.Poll(1720, true) == Action::Replay, "sent again on the new subscription");
    }
    {
        // The report times out seconds later (the controller lost the session), then it comes back
        GestureReplay r = make();
        press(r, 1000, Delivery::Live);
        check(r.Poll(9000, true) == Action::None && r.AwaitingConfirm(), "no answer yet: still watched");
        r.OnSubscriptionTerminated(0);
        check(r.Pending(), "report timeout: the gesture waits");
        r.OnSubscriptionEstablished(20000);
        check(r.Poll(20400, true) == Action::Replay, "sent again when the controller is back within the window");
    }
    {
        GestureReplay r = make();
        press(r, 1000, Delivery::Live);
        r.OnSubscriptionTerminated(1);
        check(r.AwaitingConfirm() && !r.Pending(), "one of two controllers closed: the other one is still watched");
        r.OnReportConfirmed();
        r.OnSubscriptionTerminated(0);
        check(!r.Pending() && r.NextDeadline() == GestureReplay::kNoDeadline,
              "the other controller acknowledged: no replay (it would get the gesture twice)");
    }
    {
        // Never acknowledged, never closed (report still on its way): forgotten at the end of the window
        GestureReplay r = make();
        press(r, 1000, Delivery::Live);
        check(r.NextDeadline() == 31000 && r.Poll(31000, true) == Action::None && !r.AwaitingConfirm(),
              "not acknowledged within the window: no longer watched");
        r.OnSubscriptionTerminated(0);
        r.OnSubscriptionEstablished(40000);
        check(!r.Pending() && r.Poll(40400, true) == Action::None, "closed after the window: no late action");
    }
    {
        // Pressed again while waiting: one action, not two
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        r.OnGestureStart();
        check(!r.Pending() && r.NextDeadline() == GestureReplay::kNoDeadline, "new gesture: the waiting one is forgotten");
        const bool now = r.OnDecisive(Kind::Presses, 1, Delivery::Live);
        r.OnGestureEnd(3000, true, true);
        r.OnReportConfirmed();
        r.OnSubscriptionEstablished(3500);
        check(now && r.Poll(3900, true) == Action::None, "the new one went live: no replay of either");
    }
    {
        // The replay itself is refused (a resumed subscription the controller does not know)
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        r.OnSubscriptionEstablished(2000);
        check(r.Poll(2400, true) == Action::Replay, "first replay");
        r.OnSubscriptionTerminated(0);
        check(r.Pending(), "replay refused: waits again");
        r.OnSubscriptionEstablished(4000);
        check(r.Poll(4400, true) == Action::Replay, "second replay on the next subscription");
        r.OnSubscriptionTerminated(0);
        check(r.Pending() && r.Poll(31000, false) == Action::Dropped, "the window still counts from the gesture");
    }
    {
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        r.OnSubscriptionEstablished(2000);
        r.OnSubscriptionTerminated(0);
        check(r.Poll(2400, false) == Action::None && r.Pending(), "subscription gone before the replay: still waits");
        r.OnSubscriptionEstablished(5000);
        check(r.Poll(5400, true) == Action::Replay, "sent on the next one");
    }
    {
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        r.OnSubscriptionEstablished(2000);
        check(r.Poll(2400, false) == Action::None && r.Pending(), "established but the network is down: not sent yet");
    }
    {
        // Long press: decisive at LongPress, the gesture ends at the release
        GestureReplay r = make();
        r.OnGestureStart();
        check(r.OnDecisive(Kind::Long, 1, Delivery::None) && !r.Pending(), "long press held: not waiting yet");
        r.OnSubscriptionEstablished(3000);
        check(r.NextDeadline() == GestureReplay::kNoDeadline, "subscribed while held: nothing before the release");
        r.OnGestureEnd(4000, true, true);
        check(r.Pending() && r.Poll(4400, true) == Action::Replay && r.GetKind() == Kind::Long,
              "long press sent again 400 ms after the release");
    }
    {
        GestureReplay r = make();
        r.OnGestureStart();
        r.OnDecisive(Kind::Long, 1, Delivery::Live);
        check(r.AwaitingConfirm() && r.NextDeadline() == GestureReplay::kNoDeadline, "long press held: watched, no window yet");
        r.OnSubscriptionTerminated(0); // refused while the button is still held
        check(!r.Pending() && r.Poll(2350, false) == Action::None, "refused while held: decided at the release");
        r.OnGestureEnd(5000, true, false);
        check(r.Pending(), "released: waits");
    }
    {
        // Maintenance hold (pairing window, factory reset) or aborted sequence: never sent again
        GestureReplay r = make();
        r.OnGestureStart();
        r.OnDecisive(Kind::Long, 1, Delivery::Live);
        r.OnGestureEnd(12000, false, false);
        check(!r.Pending() && !r.AwaitingConfirm() && r.NextDeadline() == GestureReplay::kNoDeadline,
              "not replayable: forgotten");
        r.OnSubscriptionTerminated(0);
        r.OnSubscriptionEstablished(13000);
        check(r.Poll(13400, true) == Action::None, "not replayable: no replay");
    }
    {
        // The SubscribeResponse is not acknowledged yet: the replay waits, still inside the window
        GestureReplay r = make();
        press(r, 1000, Delivery::None);
        r.DelayReplay(1100);
        check(r.NextDeadline() == 31000, "no replay scheduled: nothing to delay");
        r.OnSubscriptionEstablished(2000);
        r.DelayReplay(2300);
        check(r.NextDeadline() == 2400, "a replay that is not due yet is not moved");
        r.DelayReplay(2500);
        check(r.NextDeadline() == 2500 && r.Poll(2400, true) == Action::None, "due replay delayed");
        check(r.Poll(2500, true) == Action::Replay, "sent once the delay is over");
        GestureReplay late = make();
        press(late, 1000, Delivery::None);
        late.OnSubscriptionEstablished(30000);
        late.DelayReplay(40000);
        check(late.NextDeadline() == 31000 && late.Poll(31000, true) == Action::Dropped,
              "delayed past the window: dropped, never a late action");
    }
    {
        GestureReplay r = make();
        check(!press(r, 1000, Delivery::Priming), "held back");
        check(r.Poll(31000, false) == Action::Dropped, "held back, never established: dropped at the end of the window");
    }

    std::printf("gesture replay: %s\n", s_failures ? "FAILED" : "all tests passed");
    return s_failures ? 1 : 0;
}
