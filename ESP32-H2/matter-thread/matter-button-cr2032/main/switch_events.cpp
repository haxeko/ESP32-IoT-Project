// See switch_events.h. esp-matter 1.6.1 (connectedhomeip code-driven SwitchCluster).

#include <sdkconfig.h>

#include "switch_events.h"

#include <esp_log.h>

#include <app/EventManagement.h>
#include <app/clusters/switch-server/CodegenIntegration.h>
#include <app/icd/server/ICDServerConfig.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#if CHIP_CONFIG_ENABLE_ICD_LIT
#include <app/icd/server/ICDNotifier.h>
#endif

#include "battery.h"
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

/* CHIP task: one gesture, packed by post_gesture() */
static void handle_gesture(intptr_t packed)
{
    const auto type = static_cast<ButtonEvent>(packed & 0xFF);
    const auto count = static_cast<uint8_t>((packed >> 8) & 0xFF);
    const auto action = static_cast<ButtonAction>((packed >> 16) & 0xFF);

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
#if CHIP_CONFIG_ENABLE_ICD_LIT
        // LIT User Active Mode Trigger (UserActiveModeTriggerHint = ActuateSensor): stay reachable for a while
        chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
#endif
        power_on_user_activity();
        break;
    case ButtonEvent::Release:
        LogErrorOnFailure(sw->SetCurrentPosition(k_idle_position));
#if !CONFIG_SWITCH_ACTION
        logged |= sw->OnShortRelease(k_pressed_position).has_value();
#endif
#if !CONFIG_SWITCH_MULTI_PRESS
        battery_hint();
#endif
        break;
    case ButtonEvent::MultiDone:
#if CONFIG_SWITCH_MULTI_PRESS
        logged |= sw->OnMultiPressComplete(k_pressed_position, count <= k_multi_press_max ? count : 0).has_value();
#endif
        battery_hint();
        break;
    case ButtonEvent::LongStart:
        logged |= sw->OnLongPress(k_pressed_position).has_value();
        break;
    case ButtonEvent::LongRelease:
        LogErrorOnFailure(sw->SetCurrentPosition(k_idle_position));
        logged |= sw->OnLongRelease(k_pressed_position).has_value();
        handle_maintenance(action);
        battery_hint();
        break;
    case ButtonEvent::HoldPairing:
    case ButtonEvent::HoldReset:
    case ButtonEvent::HoldCancel:
        break; // LED only, see post_gesture()
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
