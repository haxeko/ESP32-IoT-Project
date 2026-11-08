// See door_events.h. esp-matter 1.6.1 (connectedhomeip code-driven BooleanStateCluster).

#include <sdkconfig.h>

#include "door_events.h"

#include <esp_log.h>

#include <app/EventManagement.h>
#include <app/clusters/boolean-state-server/CodegenIntegration.h>
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

static const char *TAG = "door";

static uint16_t s_endpoint = 0;

// Packed into the ScheduleWork argument
static constexpr intptr_t k_closed = 1 << 0;
static constexpr intptr_t k_initial = 1 << 1;
static constexpr intptr_t k_gesture = 1 << 2;

uint16_t door_events_create_endpoint(node_t *node, bool closed)
{
    endpoint::contact_sensor::config_t cfg;
#if !STATUS_LED_PRESENT
    // No LED to show Identify
    cfg.identify.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kNone);
#endif
    // The code-driven cluster starts at false anyway: the contact task sets the real state once it runs
    cfg.boolean_state.state_value = closed;
    endpoint_t *ep = endpoint::contact_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) {
        return 0;
    }
    s_endpoint = endpoint::get_id(ep);
    return s_endpoint;
}

void door_events_identify(bool on)
{
    status_led_set(LedMode::Identify, on);
}

void door_events_factory_reset(void)
{
    // Called on the CHIP task. Show the confirmation, then erase fabrics, Thread credentials and settings
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

/* The event just logged goes out now (still after the subscription's min interval), even to a controller that did
 * not ask for it as urgent: otherwise it would wait for the next report, up to the IdleModeDuration */
static void deliver_now()
{
#if CONFIG_CONTACT_FORCE_EVENT_DELIVERY
    chip::app::EventManagement::GetInstance().ScheduleUrgentEventDeliverySync();
#endif
}

static void battery_hint()
{
    if (battery_level() != BatteryLevel::Ok) {
        status_led_flash(LedFlash::LowBattery);
    }
}

static void set_state(bool closed)
{
    BooleanStateCluster *bs = BooleanState::FindClusterOnEndpoint(s_endpoint);
    if (!bs) {
        ESP_LOGE(TAG, "No Boolean State cluster on endpoint %u", s_endpoint);
        return;
    }
    // Attribute report and StateChange event only when the value changes
    if (bs->SetStateValue(closed).has_value()) {
        deliver_now();
    }
}

/* CHIP task: one contact event, packed by door_events_on_contact() */
static void handle_event(intptr_t packed)
{
    const bool closed = (packed & k_closed) != 0;
    if (packed & k_gesture) {
        open_commissioning_window();
        return;
    }
    set_state(closed);
    if (packed & k_initial) {
        return;
    }

    battery_note_activity(); // the report and the active mode load the cell: no rest reading right after
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        // Not commissioned yet: a door movement (re)opens the BLE commissioning window
        open_commissioning_window();
        return;
    }
#if CHIP_CONFIG_ENABLE_ICD_LIT
    // LIT User Active Mode Trigger (UserActiveModeTriggerHint = ActuateSensor): stay reachable for a while
    chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
#endif
    power_on_user_activity();
    battery_hint();
}

void door_events_on_contact(const ContactEventInfo &event, bool initial)
{
    // Contact task: LED feedback at once, Matter work on the CHIP task
    intptr_t packed = event.closed ? k_closed : 0;
    if (event.type == ContactEvent::Gesture) {
        ESP_LOGI(TAG, "Magnet gesture: commissioning window");
        packed |= k_gesture;
    } else {
#if CONFIG_STATUS_LED_CHANGE_FEEDBACK
        if (!initial) {
            status_led_flash(LedFlash::Ack);
        }
#endif
        ESP_LOGI(TAG, "Door %s%s", event.closed ? "closed" : "open", initial ? " (at start)" : "");
        if (initial) {
            packed |= k_initial;
        }
    }
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(handle_event, packed) != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Contact event dropped: Matter stack not ready");
    }
}
