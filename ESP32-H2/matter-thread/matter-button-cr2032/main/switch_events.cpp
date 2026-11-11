// See switch_events.h. esp-matter 1.6.1 (connectedhomeip code-driven SwitchCluster).

#include <sdkconfig.h>

#include "switch_events.h"

#include <esp_log.h>

#include <app/EventManagement.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/clusters/switch-server/CodegenIntegration.h>
#include <app/icd/server/ICDServerConfig.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#if CHIP_CONFIG_ENABLE_ICD_SERVER
#include <app/icd/server/ICDNotifier.h>
#endif

#include "battery.h"
#include "gesture_replay.h"
#include "power.h"
#include "setup_code.h"
#include "status_led.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "switch";

static constexpr uint8_t k_idle_position = 0;
static constexpr uint8_t k_pressed_position = 1;
#if CONFIG_SWITCH_MULTI_PRESS
static constexpr uint8_t k_multi_press_max = CONFIG_SWITCH_MULTI_PRESS_MAX;
#endif

// Undelivered gestures are sent again (gesture_replay.h). Needs MultiPressComplete as the event controllers act on, and
// the forced delivery: it marks every subscription dirty, which is how report_confirmed() knows a report is still due
#if defined(CONFIG_SWITCH_REPLAY_WINDOW_S) && CONFIG_SWITCH_REPLAY_WINDOW_S > 0 && CONFIG_SWITCH_FORCE_EVENT_DELIVERY
#define SWITCH_REPLAY 1
#else
#define SWITCH_REPLAY 0
#endif

static uint16_t s_endpoint = 0;

uint16_t switch_events_create_endpoint(node_t *node)
{
    endpoint::generic_switch::config_t cfg;
#if !STATUS_LED_PRESENT
    // No LED to show Identify
    cfg.identify.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kNone);
#endif
    cfg.switch_cluster.number_of_positions = 2;
    cfg.switch_cluster.current_position = k_idle_position;
    cfg.switch_cluster.feature_flags = cluster::switch_cluster::feature::momentary_switch::get_id();
    endpoint_t *ep = endpoint::generic_switch::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) {
        return 0;
    }
    cluster_t *sw = cluster::get(ep, Switch::Id);
    if (!sw) {
        return 0;
    }
    // Each add() checks the features it depends on, so the order matters
    esp_err_t err = ESP_OK;
#if CONFIG_SWITCH_ACTION
    err |= cluster::switch_cluster::feature::action_switch::add(sw);
#else
    err |= cluster::switch_cluster::feature::momentary_switch_release::add(sw);
#endif
    err |= cluster::switch_cluster::feature::momentary_switch_long_press::add(sw);
#if CONFIG_SWITCH_MULTI_PRESS
    cluster::switch_cluster::feature::momentary_switch_multi_press::config_t msm;
    msm.multi_press_max = k_multi_press_max;
    err |= cluster::switch_cluster::feature::momentary_switch_multi_press::add(sw, &msm);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Switch feature setup failed");
        return 0;
    }
    s_endpoint = endpoint::get_id(ep);
    return s_endpoint;
}

void switch_events_identify(bool on)
{
    status_led_set(LedMode::Identify, on);
}

void switch_events_factory_reset(void)
{
    // Called on the CHIP task. Show the confirmation, then erase fabrics, Thread credentials and settings
    status_led_set(LedMode::HoldReset, false);
    status_led_set(LedMode::HoldPairing, false);
    status_led_flash(LedFlash::ResetConfirm);
    ESP_LOGW(TAG, "Factory reset");
    chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Milliseconds32(status_led_flash_duration_ms(LedFlash::ResetConfirm)),
        [](chip::System::Layer *, void *) { esp_matter::factory_reset(); }, nullptr);
}

/* CHIP task. Not commissioned: BLE + DNS-SD (first pairing). Commissioned: BLE was released after commissioning
 * (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING), so DNS-SD over Thread only: enough for a second controller with the
 * original QR / manual code. A controller's own "share / pairing mode" (enhanced window) is preferred for that. */
static void open_commissioning_window()
{
    chip::Server &server = chip::Server::GetInstance();
    chip::CommissioningWindowManager &cwm = server.GetCommissioningWindowManager();
    if (cwm.IsCommissioningWindowOpen()) {
        ESP_LOGI(TAG, "Commissioning window already open");
        return;
    }
    const bool commissioned = server.GetFabricTable().FabricCount() > 0;
    const CHIP_ERROR err = cwm.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS),
        commissioned ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                     : chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Commissioning window open for %d s (%s)", CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS,
                 commissioned ? "Thread/DNS-SD" : "BLE + DNS-SD");
        setup_code_print(commissioned);
    } else {
        // e.g. a commissioning (fail-safe) still in progress
        ESP_LOGW(TAG, "Cannot open the commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

/* The events just logged go out now (still after the subscription's min interval), even to a controller that
 * did not ask for them as urgent: otherwise they would wait for the next report, up to the IdleModeDuration */
static void deliver_now()
{
#if CONFIG_SWITCH_FORCE_EVENT_DELIVERY
    chip::app::EventManagement::GetInstance().ScheduleUrgentEventDeliverySync();
#endif
}

static void battery_hint()
{
    if (battery_level() != BatteryLevel::Ok) {
        status_led_flash(LedFlash::LowBattery);
    }
}

static void handle_maintenance(ButtonAction action)
{
    status_led_set(LedMode::HoldPairing, false);
    status_led_set(LedMode::HoldReset, false);
    if (action == ButtonAction::OpenPairing) {
        open_commissioning_window();
    } else if (action == ButtonAction::FactoryReset) {
        switch_events_factory_reset();
    }
}

static void handle_uncommissioned(ButtonEvent type, uint8_t count, ButtonAction action)
{
    switch (type) {
    case ButtonEvent::Release:
        // A short press (re)opens the BLE commissioning window: no controller to send events to yet
        if (count == 1) {
            open_commissioning_window();
        }
        break;
    case ButtonEvent::LongRelease:
        handle_maintenance(action);
        break;
    default:
        break;
    }
}

/* One button event as Switch cluster events and CurrentPosition (CHIP task). Returns true when an event was logged. */
static bool emit_events(SwitchCluster *sw, ButtonEvent type, uint8_t count)
{
    bool logged = false;
    switch (type) {
    case ButtonEvent::Press:
        LogErrorOnFailure(sw->SetCurrentPosition(k_pressed_position));
#if CONFIG_SWITCH_ACTION
        // Action switch: one InitialPress per sequence (TC-SWTCH-2.6)
        if (count == 1) {
            logged |= sw->OnInitialPress(k_pressed_position).has_value();
        }
#else
        logged |= sw->OnInitialPress(k_pressed_position).has_value();
#if CONFIG_SWITCH_MULTI_PRESS
        // CurrentNumberOfPressesCounted is 2..MultiPressMax; past the max the sequence ends as aborted (count 0)
        if (count >= 2 && count <= k_multi_press_max) {
            logged |= sw->OnMultiPressOngoing(k_pressed_position, count).has_value();
        }
#endif
#endif
        break;
    case ButtonEvent::Release:
        LogErrorOnFailure(sw->SetCurrentPosition(k_idle_position));
#if !CONFIG_SWITCH_ACTION
        logged |= sw->OnShortRelease(k_pressed_position).has_value();
#endif
        break;
    case ButtonEvent::MultiDone:
#if CONFIG_SWITCH_MULTI_PRESS
        logged |= sw->OnMultiPressComplete(k_pressed_position, count <= k_multi_press_max ? count : 0).has_value();
#endif
        break;
    case ButtonEvent::LongStart:
        logged |= sw->OnLongPress(k_pressed_position).has_value();
        break;
    case ButtonEvent::LongRelease:
        LogErrorOnFailure(sw->SetCurrentPosition(k_idle_position));
        logged |= sw->OnLongRelease(k_pressed_position).has_value();
        break;
    case ButtonEvent::HoldPairing:
    case ButtonEvent::HoldReset:
    case ButtonEvent::HoldCancel:
        break; // LED only, see switch_events_on_button()
    }
    return logged;
}

/* ---------------------------------------------------------------------------------------------------------------
 * A gesture no controller could receive is sent again once one has subscribed (CHIP task only). The decisions are
 * in gesture_replay.h; here: what can receive an event right now, the subscription callbacks, the timer, the events. */
#if SWITCH_REPLAY
namespace {

using ReplayKind = GestureReplay::Kind;
using ReplayDelivery = GestureReplay::Delivery;

// Shorter than CONFIG_ICD_ACTIVE_MODE_THRESHOLD_MS, so the ICD stays in Active mode between two ticks
constexpr chip::System::Clock::Milliseconds32 k_active_tick_ms(800);
// How often the reports are looked at until a controller acknowledged the decisive event (a CHIP timer, no radio)
constexpr int64_t k_confirm_poll_ms = 100;
// After "established" the SubscribeResponse can still be on its way (lost once, MRP sends it again ~2 s later)
constexpr int64_t k_settle_ms = 5000;

GestureReplay s_replay;
uint32_t s_established = 0;   // subscriptions fully established (OnSubscriptionEstablished .. OnSubscriptionTerminated)
int64_t s_established_at = -k_settle_ms; // when the last one was
bool s_decisive_held = false; // the decisive event of the gesture in progress was held back
bool s_decisive_logged = false; // the decisive event last generated (first time or sent again) is in the event log
bool s_was_pending = false;
bool s_active_tick = false;
const char *s_reason = "";    // why the gesture has to wait, for the log

void replay_timer(chip::System::Layer *, void *);

int64_t now_ms()
{
    return static_cast<int64_t>(chip::System::SystemClock().GetMonotonicMilliseconds64().count());
}

bool thread_attached()
{
    return chip::DeviceLayer::ThreadStackMgr().IsThreadAttached();
}

bool can_send()
{
    return s_established > 0 && thread_attached();
}

/* A message still waits for its acknowledgement. Right after "established" that is the SubscribeResponse: a report
 * sent now could reach the controller before it, be refused (the controller does not know the subscription yet), and
 * close the subscription on this side only, while the controller then takes it as established. */
bool exchanges_open()
{
    return chip::Server::GetInstance().GetExchangeManager().GetNumActiveExchanges() > 0;
}

ReplayDelivery delivery_now()
{
    if (!thread_attached()) {
        s_reason = "Thread detached";
        return ReplayDelivery::None;
    }
    if (s_established > 0) {
        if (now_ms() - s_established_at < k_settle_ms && exchanges_open()) {
            s_reason = "subscription just set up";
            return ReplayDelivery::Priming;
        }
        return ReplayDelivery::Live;
    }
    // Subscribe handlers that are not established yet are being primed
    if (chip::app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
            chip::app::ReadHandler::InteractionType::Subscribe) > 0) {
        s_reason = "subscription being set up";
        return ReplayDelivery::Priming;
    }
    s_reason = "no subscription";
    return ReplayDelivery::None;
}

/* Everything generated so far has reached the established subscriptions: none has data left to send (deliver_now()
 * marked them all dirty after the decisive event, and a subscription stays dirty until its report is sent), and no
 * report waits for its StatusResponse. Only read from the timer, never from a subscription callback: a report that
 * is refused or times out closes its subscription in the same call that takes it out of these counts, and
 * OnSubscriptionTerminated() has then already made the gesture wait. */
bool report_confirmed()
{
    chip::app::InteractionModelEngine *engine = chip::app::InteractionModelEngine::GetInstance();
    return s_decisive_logged && s_established > 0 && engine->GetNumDirtySubscriptions() == 0 &&
           engine->GetReportingEngine().GetNumReportsInFlight() == 0;
}

/* While a gesture waits the ICD stays in Active mode (200 ms polls): the controller's CASE and subscribe requests are
 * not held up to 15 s by the parent */
void active_tick(chip::System::Layer *, void *)
{
    if (!s_replay.Pending()) {
        s_active_tick = false;
        return;
    }
#if CHIP_CONFIG_ENABLE_ICD_SERVER
    chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
#endif
    LogErrorOnFailure(chip::DeviceLayer::SystemLayer().StartTimer(k_active_tick_ms, active_tick, nullptr));
}

/* After every call into s_replay: the timer for its next deadline, and the start of a wait */
void replay_update()
{
    chip::DeviceLayer::SystemLayer().CancelTimer(replay_timer, nullptr);
    const int64_t now = now_ms();
    int64_t deadline = s_replay.NextDeadline();
    if (s_replay.AwaitingConfirm() && now + k_confirm_poll_ms < deadline) {
        deadline = now + k_confirm_poll_ms;
    }
    if (deadline != GestureReplay::kNoDeadline) {
        const int64_t wait = deadline - now;
        LogErrorOnFailure(chip::DeviceLayer::SystemLayer().StartTimer(
            chip::System::Clock::Milliseconds32(wait > 0 ? static_cast<uint32_t>(wait) : 0), replay_timer, nullptr));
    }
    const bool pending = s_replay.Pending();
    if (pending && !s_was_pending) {
        ESP_LOGW(TAG, "Gesture not delivered (%s): sent again if a controller subscribes within %d s", s_reason,
                 CONFIG_SWITCH_REPLAY_WINDOW_S);
        if (!s_active_tick) {
            s_active_tick = true;
            active_tick(nullptr, nullptr);
        }
    }
    s_was_pending = pending;
}

/* The whole gesture again, as new events: they follow the priming report, so the controller takes them as live */
void replay_events(int64_t now)
{
    SwitchCluster *sw = Switch::FindClusterOnEndpoint(s_endpoint);
    if (!sw) {
        return;
    }
    const uint8_t count = s_replay.GetCount();
    bool logged = false;
    if (s_replay.GetKind() == ReplayKind::Long) {
        logged |= emit_events(sw, ButtonEvent::Press, 1);
        s_decisive_logged = emit_events(sw, ButtonEvent::LongStart, 1);
        logged |= emit_events(sw, ButtonEvent::LongRelease, 1);
    } else {
        for (uint8_t i = 1; i <= count; i++) {
            logged |= emit_events(sw, ButtonEvent::Press, i);
            logged |= emit_events(sw, ButtonEvent::Release, i);
        }
        s_decisive_logged = emit_events(sw, ButtonEvent::MultiDone, count);
    }
    logged |= s_decisive_logged;
    ESP_LOGI(TAG, "Gesture sent again (%s, count %u) %ld ms after it ended",
             s_replay.GetKind() == ReplayKind::Long ? "long press" : "press", count,
             static_cast<long>(now - s_replay.GestureEnd()));
    if (logged) {
        deliver_now();
    }
}

void replay_timer(chip::System::Layer *, void *)
{
    const int64_t now = now_ms();
    if (s_replay.AwaitingConfirm() && report_confirmed()) {
        s_replay.OnReportConfirmed();
        ESP_LOGI(TAG, "Gesture acknowledged by a controller");
    }
    if (s_replay.Pending() && exchanges_open()) {
        s_replay.DelayReplay(now + k_confirm_poll_ms); // not before the SubscribeResponse is acknowledged
    }
    switch (s_replay.Poll(now, can_send())) {
    case GestureReplay::Action::Replay:
        replay_events(now);
        break;
    case GestureReplay::Action::Dropped:
        ESP_LOGW(TAG, "Gesture dropped: no subscription within %d s", CONFIG_SWITCH_REPLAY_WINDOW_S);
        break;
    case GestureReplay::Action::None:
        break;
    }
    replay_update();
}

/* Chained in front of a callback registered before (identity_diag's subscription log) */
class SubscriptionHook : public chip::app::ReadHandler::ApplicationCallback
{
public:
    chip::app::ReadHandler::ApplicationCallback *mPrev = nullptr;

    CHIP_ERROR OnSubscriptionRequested(chip::app::ReadHandler &handler,
                                       chip::Transport::SecureSession &session) override
    {
        return mPrev ? mPrev->OnSubscriptionRequested(handler, session) : CHIP_NO_ERROR;
    }
    void OnSubscriptionEstablished(chip::app::ReadHandler &handler) override
    {
        s_established++;
        s_established_at = now_ms();
        s_replay.OnSubscriptionEstablished(s_established_at);
        replay_update();
        if (mPrev) {
            mPrev->OnSubscriptionEstablished(handler);
        }
    }
    // Only called for a subscription that was established (ReadHandler::~ReadHandler)
    void OnSubscriptionTerminated(chip::app::ReadHandler &handler) override
    {
        if (s_established > 0) {
            s_established--;
        }
        s_reason = "subscription closed before the report was acknowledged";
        s_replay.OnSubscriptionTerminated(s_established);
        replay_update();
        if (mPrev) {
            mPrev->OnSubscriptionTerminated(handler);
        }
    }
};

SubscriptionHook s_hook;

/* First press of a gesture */
void gesture_start()
{
    ESP_LOGI(TAG, "Press: Thread %s, %" PRIu32 " subscription(s) established",
             thread_attached() ? "attached" : "DETACHED", s_established);
    if (s_replay.Pending()) {
        ESP_LOGI(TAG, "The gesture that waited is replaced by this one");
    }
    s_decisive_held = false;
    s_decisive_logged = false;
    s_replay.OnGestureStart();
    replay_update();
}

/* Before the event controllers act on (MultiPressComplete, LongPress). Returns true to generate it now. */
bool gesture_decisive(ReplayKind kind, uint8_t count)
{
    const bool generate = s_replay.OnDecisive(kind, count, delivery_now());
    s_decisive_held = !generate;
    return generate;
}

void gesture_end(bool replayable)
{
    s_replay.OnGestureEnd(now_ms(), replayable, can_send());
    s_decisive_held = false;
    replay_update();
}

} // namespace
#endif // SWITCH_REPLAY

void switch_events_on_server_ready(void)
{
#if SWITCH_REPLAY
    static bool s_installed = false;
    if (s_installed) {
        return;
    }
    s_installed = true;
    GestureReplayConfig cfg;
    cfg.window_ms = CONFIG_SWITCH_REPLAY_WINDOW_S * 1000u;
    s_replay.Init(cfg);
    chip::app::InteractionModelEngine *engine = chip::app::InteractionModelEngine::GetInstance();
    // GetAppCallback() is public in ReadHandler::ManagementCallback (InteractionModelEngine overrides it privately)
    chip::app::ReadHandler::ApplicationCallback *prev =
        static_cast<chip::app::ReadHandler::ManagementCallback *>(engine)->GetAppCallback();
    if (prev != &s_hook) {
        s_hook.mPrev = prev;
        engine->RegisterReadHandlerAppCallback(&s_hook);
    }
#endif
}

/* CHIP task: one gesture, packed by switch_events_on_button() */
static void handle_gesture(intptr_t packed)
{
    const auto type = static_cast<ButtonEvent>(packed & 0xFF);
    const auto count = static_cast<uint8_t>((packed >> 8) & 0xFF);
    const auto action = static_cast<ButtonAction>((packed >> 16) & 0xFF);

    battery_note_activity(); // the report and the active mode load the cell: no rest reading right after
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        handle_uncommissioned(type, count, action);
        return;
    }
    SwitchCluster *sw = Switch::FindClusterOnEndpoint(s_endpoint);
    if (!sw) {
        ESP_LOGE(TAG, "No Switch cluster on endpoint %u", s_endpoint);
        return;
    }

    bool logged = false;
    switch (type) {
    case ButtonEvent::Press:
#if SWITCH_REPLAY
        if (count == 1) {
            gesture_start();
        }
#endif
        logged = emit_events(sw, type, count);
#if CHIP_CONFIG_ENABLE_ICD_LIT
        // LIT User Active Mode Trigger (UserActiveModeTriggerHint = ActuateSensor): stay reachable for a while
        chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
#endif
        power_on_user_activity();
        break;
    case ButtonEvent::Release:
        logged = emit_events(sw, type, count);
#if !CONFIG_SWITCH_MULTI_PRESS
        battery_hint();
#endif
        break;
    case ButtonEvent::MultiDone: {
#if SWITCH_REPLAY
        // More presses than MultiPressMax end as an aborted sequence (count 0): reported, never sent again
        const bool replayable = count <= k_multi_press_max;
        if (!replayable || gesture_decisive(ReplayKind::Presses, count)) {
            logged = emit_events(sw, type, count);
            s_decisive_logged = logged;
        }
        gesture_end(replayable);
#else
        logged = emit_events(sw, type, count);
#endif
        battery_hint();
        break;
    }
    case ButtonEvent::LongStart:
#if SWITCH_REPLAY
        if (!gesture_decisive(ReplayKind::Long, 1)) {
            break; // LongPress held back: the whole sequence is generated once a subscription is established
        }
#endif
        logged = emit_events(sw, type, count);
#if SWITCH_REPLAY
        s_decisive_logged = logged;
        replay_update(); // the report is watched from now on
#endif
        break;
    case ButtonEvent::LongRelease:
#if SWITCH_REPLAY
        if (s_decisive_held) {
            // No LongRelease without its LongPress: only the position goes back to idle
            LogErrorOnFailure(sw->SetCurrentPosition(k_idle_position));
        } else {
            logged = emit_events(sw, type, count);
        }
        // A maintenance hold (pairing window, factory reset) is not an automation trigger to send again
        gesture_end(action == ButtonAction::None);
#else
        logged = emit_events(sw, type, count);
#endif
        handle_maintenance(action);
        battery_hint();
        break;
    case ButtonEvent::HoldPairing:
    case ButtonEvent::HoldReset:
    case ButtonEvent::HoldCancel:
        break; // LED only, see switch_events_on_button()
    }
    if (logged) {
        deliver_now();
    }
}

void switch_events_on_button(const ButtonEventInfo &event)
{
    // Button task: LED feedback at once, Matter work on the CHIP task
    switch (event.type) {
    case ButtonEvent::Press:
#if CONFIG_STATUS_LED_PRESS_FEEDBACK
        status_led_flash(LedFlash::Ack);
#endif
        break;
    case ButtonEvent::HoldPairing:
        status_led_set(LedMode::HoldPairing, true);
        break;
    case ButtonEvent::HoldReset:
        status_led_set(LedMode::HoldPairing, false);
        status_led_set(LedMode::HoldReset, true);
        break;
    case ButtonEvent::HoldCancel:
        status_led_set(LedMode::HoldPairing, false);
        status_led_set(LedMode::HoldReset, false);
        break;
    default:
        break;
    }
    ESP_LOGI(TAG, "Gesture %u count %u hold %" PRIu32 " ms action %u", static_cast<unsigned>(event.type), event.count,
             event.hold_ms, static_cast<unsigned>(event.action));
    const intptr_t packed = static_cast<intptr_t>(static_cast<uint32_t>(event.type) |
                                                  (static_cast<uint32_t>(event.count) << 8) |
                                                  (static_cast<uint32_t>(event.action) << 16));
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(handle_gesture, packed) != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Gesture dropped: Matter stack not ready");
    }
}
