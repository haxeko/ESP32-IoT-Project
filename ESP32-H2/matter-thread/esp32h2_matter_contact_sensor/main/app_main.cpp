/*
 * ESP32-H2 Matter over Thread contact sensor (door / window sensor), powered from USB.
 *
 * Endpoint 0: Root Node. Endpoint 1: Contact Sensor (device type 0x0015) with the Identify and Boolean State
 * clusters. A reed switch between a GPIO and GND gives the door state.
 *
 * Start-up and structure follow esp-matter release/v1.6.1 examples/sensors, the closest official example
 * (that release has no contact sensor example). Changes from it:
 *  - endpoint::contact_sensor instead of the temperature / humidity / occupancy sensors;
 *  - the GPIO interrupt only wakes a task, the debounced state is handed to the Matter task with
 *    PlatformMgr().ScheduleWork() (the example calls ScheduleLambda from the interrupt handler);
 *  - StateValue is set through the BooleanStateCluster object: in this release the attribute is managed by
 *    the cluster (ATTRIBUTE_FLAG_MANAGED_INTERNALLY), attribute::update() returns ESP_ERR_NOT_SUPPORTED for it;
 *  - own reset button driver (pin and hold time from menuconfig) instead of esp_bsp_generic + app_reset;
 *  - Thread Full Thread Device: always receiving, router-eligible, never sleepy (sdkconfig.defaults).
 */

#include <inttypes.h>

#include <atomic>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data_model_provider/clusters/boolean_state/integration.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <setup_payload/OnboardingCodesUtil.h>
#if CONFIG_ENABLE_CHIP_SHELL
#include <esp_matter_console.h>
#endif

#include <esp_openthread.h>
#include <esp_openthread_types.h>
#include <openthread/thread.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include "contact_sensor_driver.h"
#include "reset_button.h"
#include "status_led.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// ---------------------------------------------------------------------------
// Pins, checked at build time

// GPIO6-7 and GPIO15-21 connect the in-package flash (not on the module pins), GPIO26-27 are the USB
// Serial/JTAG port used for flashing and logs.
static constexpr bool is_reserved_pin(int gpio)
{
    return (gpio >= 6 && gpio <= 7) || (gpio >= 15 && gpio <= 21) || gpio == 26 || gpio == 27;
}
static_assert(!is_reserved_pin(CONFIG_CONTACT_SENSOR_GPIO),
              "CONTACT_SENSOR_GPIO is a flash or USB pin (GPIO6-7, GPIO15-21, GPIO26-27)");
static_assert(!is_reserved_pin(CONFIG_FACTORY_RESET_GPIO),
              "FACTORY_RESET_GPIO is a flash or USB pin (GPIO6-7, GPIO15-21, GPIO26-27)");
static_assert(CONFIG_CONTACT_SENSOR_GPIO != CONFIG_FACTORY_RESET_GPIO,
              "The reed switch and the reset button need different GPIOs");
#if CONFIG_STATUS_LED_ENABLE
static_assert(!is_reserved_pin(CONFIG_STATUS_LED_GPIO) && CONFIG_STATUS_LED_GPIO != CONFIG_CONTACT_SENSOR_GPIO &&
                  CONFIG_STATUS_LED_GPIO != CONFIG_FACTORY_RESET_GPIO,
              "STATUS_LED_GPIO is a flash/USB pin or is shared with the reed switch or the button");
#endif

#if CONFIG_CONTACT_ACTIVE_LOW
static constexpr bool kClosedLevelLow = true;
#else
static constexpr bool kClosedLevelLow = false;
#endif

// Window reopened after the last fabric is removed, as in the esp-matter example
static constexpr uint16_t kRecommissioningWindowSec = 300;
// Retry delay when the Matter event queue is full or the cluster is not registered yet
static constexpr uint64_t kSyncRetryUs = 200 * 1000;

// ---------------------------------------------------------------------------
// Contact state -> Matter

// Contact Sensor device type (0x0015), Matter Device Library specification: Boolean State StateValue is
//   TRUE  = closed, or contact
//   FALSE = open, or no contact
// (The SDK only declares StateValue as a read-only bool, connectedhomeip data_model/1.5.1/clusters/
// BooleanState.xml; the meaning comes from the device type. Home Assistant inverts the value for its door
// entity for that reason.)
static constexpr bool contact_state_to_matter_state_value(contact_state_t state)
{
    return state == CONTACT_STATE_CLOSED;
}

// The whole chain from the pin, for CONFIG_CONTACT_ACTIVE_LOW=y (default, normally-open reed switch to GND):
//   GPIO LOW  (magnet at the switch, contact closed) -> CONTACT_STATE_CLOSED -> StateValue true
//   GPIO HIGH (magnet away, pull-up)                 -> CONTACT_STATE_OPEN   -> StateValue false
// CONFIG_CONTACT_ACTIVE_LOW=n swaps the two GPIO levels.
static bool reed_level_to_matter_state(int level)
{
    return contact_state_to_matter_state_value(reed_level_to_contact_state(level, kClosedLevelLow));
}

static uint16_t s_contact_endpoint_id = chip::kInvalidEndpointId;
static std::atomic<bool> s_matter_started{false};
// A sync is queued on the Matter task; it reads the latest state when it runs, so one is enough
static std::atomic<bool> s_sync_scheduled{false};
static esp_timer_handle_t s_sync_retry_timer;
// Matter task only: BLE was shut down after commissioning (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING)
static bool s_ble_released;

static void start_sync_retry()
{
    // ESP_ERR_INVALID_STATE: a retry is already pending
    esp_err_t err = esp_timer_start_once(s_sync_retry_timer, kSyncRetryUs);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Cannot start the retry timer: %s", esp_err_to_name(err));
    }
}

// Runs on the Matter task, with the Matter stack lock held (work scheduled with ScheduleWork).
static void sync_contact_state(intptr_t)
{
    // Cleared before reading the state: a change after this point schedules a new sync
    s_sync_scheduled.store(false);

    BooleanStateCluster *cluster = BooleanState::FindClusterOnEndpoint(s_contact_endpoint_id);
    if (cluster == nullptr) {
        ESP_LOGW(TAG, "Boolean State cluster on endpoint %u not ready, retrying", s_contact_endpoint_id);
        start_sync_retry();
        return;
    }

    const contact_state_t state = contact_sensor_get_state();
    const bool value = contact_state_to_matter_state_value(state);
    if (cluster->GetStateValue() == value) {
        // Already the reported value: no second write, no duplicate StateChange event
        return;
    }
    // Sets the attribute and marks it dirty, so every subscriber gets a report as soon as the minimum
    // interval of its subscription allows, and records a StateChange event
    cluster->SetStateValue(value);
    ESP_LOGI(TAG, "Matter: door %s, StateValue = %s", contact_state_to_str(state), value ? "true" : "false");
}

// Any task. Never blocks: if the Matter event queue is full, retries from a timer.
static void schedule_contact_sync()
{
    if (!s_matter_started.load()) {
        // Synced once right after esp_matter::start()
        return;
    }
    if (s_sync_scheduled.exchange(true)) {
        return;
    }
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(sync_contact_state);
    if (err != CHIP_NO_ERROR) {
        s_sync_scheduled.store(false);
        ESP_LOGW(TAG, "Cannot schedule the Matter update (%" CHIP_ERROR_FORMAT "), retrying", err.Format());
        start_sync_retry();
    }
}

// esp_timer task
static void sync_retry_timer_cb(void *arg)
{
    schedule_contact_sync();
}

// Contact driver task: the debounced state changed
static void on_contact_change(contact_state_t state, void *user_data)
{
    status_led_set_contact_open(state == CONTACT_STATE_OPEN);
    schedule_contact_sync();
}

// ---------------------------------------------------------------------------
// Matter events

static bool is_commissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

static void print_onboarding_codes()
{
#if CONFIG_CONTACT_PRINT_ONBOARDING_CODES
    // Once BLE is released the device can only be found on the Thread network (DNS-SD over SRP)
    const auto flags = s_ble_released ? chip::RendezvousInformationFlag::kOnNetwork
                                      : chip::RendezvousInformationFlag::kBLE;
    PrintOnboardingCodes(chip::RendezvousInformationFlags(flags));
#endif
}

// Matter task
static void update_commissioning_led()
{
    status_led_set_commissioning(chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen());
}

// Matter task. Same behaviour as the esp-matter example: only when no fabric is left. A device that is
// still commissioned never opens a window on its own (not after a reboot, not after a Thread outage).
static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(!is_commissioned());

    chip::CommissioningWindowManager &commission_mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(!commission_mgr.IsCommissioningWindowOpen());

    // After the last fabric is removed the Thread credentials are kept and the device stays on the network;
    // BLE has been shut down after the first commissioning, so advertise on DNS-SD only.
    const auto advertisement = s_ble_released ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                                              : chip::CommissioningWindowAdvertisement::kAllSupported;
    CHIP_ERROR err = commission_mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(kRecommissioningWindowSec),
                                                                 advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    ESP_LOGI(TAG, "Commissioning window open for %u s", kRecommissioningWindowSec);
    print_onboarding_codes();
}

// Matter task
static void log_thread_role()
{
    otInstance *instance = esp_openthread_get_instance();
    VerifyOrReturn(instance != nullptr);
    chip::DeviceLayer::ThreadStackMgr().LockThreadStack();
    const otDeviceRole role = otThreadGetDeviceRole(instance);
    chip::DeviceLayer::ThreadStackMgr().UnlockThreadStack();
    ESP_LOGI(TAG, "Thread role: %s", otThreadDeviceRoleToString(role));
}

// Matter task
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    using namespace chip::DeviceLayer;

    switch (event->Type) {
    case DeviceEventType::kServerReady:
        if (is_commissioned()) {
            ESP_LOGI(TAG, "Commissioned (%u fabric(s)), Thread credentials restored from flash",
                     chip::Server::GetInstance().GetFabricTable().FabricCount());
        } else {
            ESP_LOGI(TAG, "Not commissioned: waiting for a controller (BLE)");
            print_onboarding_codes();
        }
        break;

    case DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete (fabric index %u)", event->CommissioningComplete.fabricIndex);
        break;

    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed, fail-safe timer expired: changes rolled back");
        break;

    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case DeviceEventType::kThreadConnectivityChange:
        if (event->ThreadConnectivityChange.Result == kConnectivity_Established) {
            ESP_LOGI(TAG, "Thread attached");
        } else if (event->ThreadConnectivityChange.Result == kConnectivity_Lost) {
            // Nothing to do: OpenThread keeps trying to re-attach with the stored dataset, the fabrics and the
            // Thread credentials stay, and controllers re-subscribe once the device is reachable again.
            ESP_LOGW(TAG, "Thread detached: re-attaching with the stored network");
        }
        break;

    case DeviceEventType::kThreadStateChange:
        if (event->ThreadStateChange.RoleChanged) {
            log_thread_role();
        }
        break;

    case DeviceEventType::kFabricRemoved:
        ESP_LOGW(TAG, "Fabric removed (%u left)", chip::Server::GetInstance().GetFabricTable().FabricCount());
        open_commissioning_window_if_necessary();
        break;

    case DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        s_ble_released = true;
        break;

    default:
        break;
    }
    update_commissioning_led();
}

// Identify cluster (mandatory on the Contact Sensor endpoint): blink the LED if there is one.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: type %u, effect %u, variant %u", type, effect_id, effect_variant);
    if (type == identification::START) {
        status_led_set_identify(true);
    } else if (type == identification::STOP) {
        status_led_set_identify(false);
    }
    return ESP_OK;
}

// Writes from controllers. Nothing on this device is controlled: StateValue is read-only (the Interaction
// Model rejects writes to it before this callback) and only reflects the reed switch. Other writable
// attributes (e.g. Identify, NodeLabel) are handled by their clusters, so just accept them.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Factory reset button (button task)

static void reset_hold_start(void *)
{
    ESP_LOGW(TAG, "Keep holding for factory reset (%d ms)", CONFIG_FACTORY_RESET_HOLD_MS);
    status_led_set_reset_hold(true);
}

static void reset_hold_cancel(void *)
{
    status_led_set_reset_hold(false);
}

static void reset_hold_complete(void *)
{
    ESP_LOGW(TAG, "Factory reset: removing all Matter fabrics and the Thread network, then restarting");
    status_led_set_resetting();
    if (s_matter_started.load()) {
        // Erases the esp-matter attributes, then schedules the reset on the Matter task: fabrics deleted,
        // Thread dataset and Matter key-value store erased, restart
        esp_err_t err = esp_matter::factory_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_matter::factory_reset: %s", esp_err_to_name(err));
        }
    } else {
        // Matter never started: erase the NVS partition directly
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        esp_restart();
    }
}

// ---------------------------------------------------------------------------

static esp_err_t create_contact_sensor_endpoint(node_t *node)
{
    // Identify (type: visible indicator) and Boolean State with its StateChange event, as the Contact Sensor
    // device type requires. config.boolean_state.state_value is not used: StateValue is held by the
    // BooleanStateCluster object, which starts at false. The real GPIO state is written right after
    // esp_matter::start().
    contact_sensor::config_t config;
    endpoint_t *endpoint = contact_sensor::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    VerifyOrReturnError(endpoint != nullptr, ESP_FAIL, ESP_LOGE(TAG, "Failed to create the contact sensor endpoint"));
    s_contact_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Contact sensor on endpoint %u", s_contact_endpoint_id);
    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Only when the NVS partition cannot be read at all (truncated, or written by an incompatible
        // NVS version): the commissioning is lost either way, start from an empty partition.
        ESP_LOGE(TAG, "NVS partition unreadable (%s): erasing it, commission the device again", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    status_led_init();

    const reset_button_config_t button_config = {
        .gpio_num = CONFIG_FACTORY_RESET_GPIO,
        .hold_ms = CONFIG_FACTORY_RESET_HOLD_MS,
        .on_hold_start = reset_hold_start,
        .on_hold_cancel = reset_hold_cancel,
        .on_hold_complete = reset_hold_complete,
        .user_data = nullptr,
    };
    err = reset_button_init(&button_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reset button not available: %s", esp_err_to_name(err));
    }

    const esp_timer_create_args_t retry_args = {
        .callback = sync_retry_timer_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "contact_sync",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_sync_retry_timer));

    // The real state of the door before Matter starts: never assumed closed
    const contact_sensor_config_t contact_config = {
        .gpio_num = CONFIG_CONTACT_SENSOR_GPIO,
        .closed_level_low = kClosedLevelLow,
        .debounce_ms = CONFIG_CONTACT_DEBOUNCE_MS,
        .on_change = on_contact_change,
        .user_data = nullptr,
    };
    err = contact_sensor_init(&contact_config);
    if (err != ESP_OK) {
        // Matter still starts, so the device can be reached and reset; the reported state never changes
        ESP_LOGE(TAG, "Reed switch driver failed: %s", esp_err_to_name(err));
    } else {
        const int level = contact_sensor_get_level();
        ESP_LOGI(TAG, "Door %s at start-up (GPIO%d level %d -> StateValue %s)",
                 contact_state_to_str(contact_sensor_get_state()), CONFIG_CONTACT_SENSOR_GPIO, level,
                 reed_level_to_matter_state(level) ? "true" : "false");
        status_led_set_contact_open(contact_sensor_get_state() == CONTACT_STATE_OPEN);
    }

    /* Matter node with the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create the Matter node");
        return;
    }
    if (create_contact_sensor_endpoint(node) != ESP_OK) {
        return;
    }

    /* OpenThread on the native 802.15.4 radio of the ESP32-H2 (no RCP, no host); Thread data in "nvs" */
    esp_openthread_platform_config_t ot_config = {
        .radio_config = { .radio_mode = RADIO_MODE_NATIVE },
        .host_config = { .host_connection_mode = HOST_CONNECTION_MODE_NONE },
        .port_config = { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10 },
    };
    set_openthread_platform_config(&ot_config);

    /* Matter start: restores fabrics and the Thread dataset from flash and re-attaches by itself; opens the
       commissioning window (BLE) only when there is no fabric */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter, err:%d", err);
        return;
    }
    s_matter_started.store(true);

    // Initial state: write the GPIO state read above (or any change since) to the cluster
    schedule_contact_sync();

#if CONFIG_ENABLE_CHIP_SHELL
    // Debug builds only (CONFIG_ENABLE_CHIP_SHELL=y, off in sdkconfig.defaults): "matter ..." console commands,
    // e.g. "matter esp ot_cli state" with CONFIG_OPENTHREAD_CLI=y
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}
