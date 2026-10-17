// Matter over Thread battery button: ESP32-H2-WROOM-02C, CR2032 through a TPS63900 buck-boost (see README).
// ESP-IDF v6.0.2, esp-matter 1.6.1. Thread Sleepy End Device + Matter ICD (SIT, or LIT with sdkconfig.defaults.lit).

#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>

#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_openthread_types.h>
#include <openthread/thread.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <app/icd/server/ICDServerConfig.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>

#include "battery.h"
#include "button.h"
#include "device_info.h"
#include "identity_diag.h"
#include "power.h"
#include "setup_code.h"
#include "status_led.h"
#include "switch_events.h"
#include "thread_diag.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

/* ESP32-H2 runs OpenThread on its own 802.15.4 radio (no RCP, no host) */
static constexpr esp_openthread_platform_config_t k_openthread_config = {
    .radio_config = {.radio_mode = RADIO_MODE_NATIVE},
    .host_config = {.host_connection_mode = HOST_CONNECTION_MODE_NONE},
    .port_config = {.storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10},
};

/* ---------------------------------------------------------------------------------------------------------------
 * OTA rollback: a new image is confirmed once attached as a Thread child for 60 s with its fabrics, and rolled back
 * if that has not happened 60 min after boot (an image that runs but never joins the network) */

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
static constexpr uint64_t k_ota_check_period_us = 60 * 1000 * 1000ULL;
static constexpr int64_t k_ota_confirm_after_us = 60 * 1000 * 1000LL;
static constexpr int64_t k_ota_rollback_after_us = 60 * 60 * 1000 * 1000LL;
static bool s_ota_pending_verify = false;
static int64_t s_ota_attached_since = 0; // CHIP task; 0 = not attached
static esp_timer_handle_t s_ota_confirm_timer = nullptr;

static bool ota_attached_as_child()
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) {
        return false;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    const bool child = otThreadGetDeviceRole(ot) == OT_DEVICE_ROLE_CHILD;
    esp_openthread_lock_release();
    return child;
}

/* CHIP task (FabricCount() needs the CHIP lock) */
static void ota_confirm_work(intptr_t)
{
    if (!s_ota_pending_verify) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    // Never confirm with 0 fabrics: after an OTA that means the new image lost its fabric table
    if (s_ota_attached_since != 0 && now - s_ota_attached_since >= k_ota_confirm_after_us &&
        chip::Server::GetInstance().GetFabricTable().FabricCount() > 0 && ota_attached_as_child()) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            s_ota_pending_verify = false;
            ESP_LOGI(TAG, "OTA image confirmed");
            return;
        }
    }
    if (now >= k_ota_rollback_after_us) {
        ESP_LOGE(TAG, "OTA image not confirmed %d min after boot: rolling back", (int)(k_ota_rollback_after_us / 60000000));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    esp_timer_start_once(s_ota_confirm_timer, k_ota_check_period_us);
}

static void ota_confirm_cb(void *)
{
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(ota_confirm_work) != CHIP_NO_ERROR) {
        esp_timer_start_once(s_ota_confirm_timer, k_ota_check_period_us);
    }
}

static void ota_check_pending_verify()
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_timer_create_args_t args = {
            .callback = ota_confirm_cb,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ota_confirm",
            .skip_unhandled_events = false,
        };
        if (esp_timer_create(&args, &s_ota_confirm_timer) == ESP_OK) {
            s_ota_pending_verify = true;
            // Checked every minute until confirmed or rolled back (an hour at most)
            esp_timer_start_once(s_ota_confirm_timer, k_ota_check_period_us);
            ESP_LOGW(TAG, "New OTA image pending verification");
        }
    }
}
#endif // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

/* ---------------------------------------------------------------------------------------------------------------
 * Matter events (CHIP task) */

// The last fabric was removed while a commissioning window was open: factory reset once the window closes
static bool s_reset_when_window_closes = false;

/* Pairing LED, and the Thread/SRP diagnostics log only while a first commissioning can happen (every 10 s it would
 * cost ~5 uA on a device left uncommissioned) */
static void commissioning_state_update()
{
    chip::Server &server = chip::Server::GetInstance();
    const bool open = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
    status_led_set(LedMode::PairingWindow, open);
    if (open && server.GetFabricTable().FabricCount() == 0) {
        thread_diag_start();
    } else {
        thread_diag_stop();
    }
}

/* Last fabric gone: erase the Thread network too and reboot into a fresh BLE commissioning window (BLE was shut down
 * after the first commissioning). The 1 s delay lets the RemoveFabric response go out first. */
static void factory_reset_if_no_fabric()
{
    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Milliseconds32(1000),
        [](chip::System::Layer *, void *) {
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
                ESP_LOGW(TAG, "Last fabric removed, factory reset");
                esp_matter::factory_reset();
            }
        },
        nullptr);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to schedule factory reset, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    identity_diag_on_event(event);
    power_on_event(event);

    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            s_ota_attached_since = esp_timer_get_time();
        } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            s_ota_attached_since = 0;
        }
#endif
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        s_reset_when_window_closes = false;
        commissioning_state_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kServerReady:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            // The SDK opened the boot commissioning window (BLE and network)
            setup_code_print(false);
        }
        commissioning_state_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        commissioning_state_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        commissioning_state_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        commissioning_state_update();
        if (s_reset_when_window_closes) {
            s_reset_when_window_closes = false;
            factory_reset_if_no_fabric();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        chip::Server &server = chip::Server::GetInstance();
        if (server.GetFabricTable().FabricCount() == 0) {
            if (server.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
                // A failed first commissioning reverting its AddNOC, or the last controller removed while a window
                // it (or the button) opened is still open: decide when the window closes, a new controller may
                // still join through it
                ESP_LOGI(TAG, "No fabric left, commissioning window open: factory reset when it closes");
                s_reset_when_window_closes = true;
            } else {
                factory_reset_if_no_fabric();
            }
        }
        break;
    }

#if CONFIG_ENABLE_OTA_REQUESTOR
    case chip::DeviceLayer::DeviceEventType::kOtaStateChanged:
        // A download keeps the radio and the flash busy for minutes: not on a weak cell
        if (event->OtaStateChanged.newState == chip::DeviceLayer::kOtaDownloadInProgress && !battery_ota_allowed()) {
            ESP_LOGW(TAG, "OTA download cancelled: battery level not OK");
            if (chip::GetRequestorInstance()) {
                chip::GetRequestorInstance()->CancelImageUpdate();
            }
        }
        break;
#endif

    default:
        break;
    }
}

/* CHIP task: Identify blinks the status LED */
static void identify_effect_end(chip::System::Layer *, void *)
{
    switch_events_identify(false);
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    using chip::app::Clusters::Identify::EffectIdentifierEnum;
    switch (type) {
    case identification::START:
        switch_events_identify(true);
        break;
    case identification::STOP:
        switch_events_identify(false);
        break;
    case identification::EFFECT:
        chip::DeviceLayer::SystemLayer().CancelTimer(identify_effect_end, nullptr);
        if (static_cast<EffectIdentifierEnum>(effect_id) == EffectIdentifierEnum::kFinishEffect ||
            static_cast<EffectIdentifierEnum>(effect_id) == EffectIdentifierEnum::kStopEffect) {
            switch_events_identify(false);
        } else {
            // Blink / Okay / Breathe / ChannelChange: a 2 s burst of the Identify pattern
            switch_events_identify(true);
            chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(2000),
                                                        identify_effect_end, nullptr);
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    // Nothing to drive: the button only reports
    return ESP_OK;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Boot: button held at power-up -> factory reset (works even when the runtime gestures cannot be used) */

/* After button_gpio_init() */
static bool boot_reset_requested()
{
    if (!button_is_pressed_raw()) {
        return false;
    }
    ESP_LOGW(TAG, "Button held at boot: keep it pressed %d ms for a factory reset", CONFIG_BOOT_RESET_HOLD_MS);
    status_led_set(LedMode::BootResetWait, true);
    const int64_t end = esp_timer_get_time() + CONFIG_BOOT_RESET_HOLD_MS * 1000LL;
    bool held = true;
    while (esp_timer_get_time() < end) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!button_is_pressed_raw()) {
            held = false;
            break;
        }
    }
    status_led_set(LedMode::BootResetWait, false);
    ESP_LOGW(TAG, "%s", held ? "Factory reset requested at boot" : "Released: normal boot");
    return held;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    status_led_init();
    /* The button pad has no input buffer after reset: configure it before anything reads it (boot guard, boot reset) */
    err = button_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Button GPIO init failed: %s", esp_err_to_name(err));
    }
    /* Battery first: a cell too weak for Matter (or a brownout loop) sends the chip back to Deep-sleep */
    battery_init();
    battery_boot_guard();
    const bool boot_reset = boot_reset_requested();

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS unusable (%s): erasing it, the device must be added again", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }

    /* Automatic Light-sleep with the digital peripherals powered down (sdkconfig) */
    power_configure();

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ota_check_pending_verify();
#endif

    node::config_t node_config;
#if CHIP_CONFIG_ENABLE_ICD_LIT
    /* LIT User Active Mode Trigger: pressing the button wakes the device (ICD Management UAT hint) */
    node_config.root_node.icd_user_active_mode_trigger.user_active_mode_trigger_hint =
        chip::to_underlying(IcdManagement::UserActiveModeTriggerBitmap::kActuateSensor);
#endif
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

#if CHIP_CONFIG_ENABLE_ICD_DSLS
    /* The SDK switches SIT/LIT dynamically (power.cpp policy): advertise it in the ICD Management FeatureMap */
    cluster_t *icd = cluster::get(endpoint::get(node, chip::kRootEndpointId), IcdManagement::Id);
    ABORT_APP_ON_FAILURE(icd != nullptr &&
                             cluster::icd_management::feature::dynamic_sit_lit_support::add(icd) == ESP_OK,
                         ESP_LOGE(TAG, "Failed to add the ICD DSLS feature"));
#endif

    /* Generic Switch (Identify + Switch) on endpoint 1 */
    const uint16_t switch_ep = switch_events_create_endpoint(node);
    ABORT_APP_ON_FAILURE(switch_ep != 0, ESP_LOGE(TAG, "Failed to create the Generic Switch endpoint"));
    ESP_LOGI(TAG, "Generic Switch on endpoint %u", switch_ep);

    /* Power Source (CR2032) on endpoint 0 */
    err = battery_create_power_source(node);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to create the Power Source cluster"));

    /* Basic Information: Manufacturer, Model, Hardware version, SerialNumber (eFuse MAC) */
    device_info_provider_init();
    /* SerialNumber is optional in Basic Information and root_node does not create it. Before esp_matter::start(),
     * which reads the cluster's optional attribute set once */
    cluster_t *basic_info = cluster::get(endpoint::get(node, chip::kRootEndpointId), BasicInformation::Id);
    ABORT_APP_ON_FAILURE(basic_info != nullptr &&
                             cluster::basic_information::attribute::create_serial_number(basic_info, nullptr, 0) !=
                                 nullptr,
                         ESP_LOGE(TAG, "Failed to add Basic Information SerialNumber attribute"));

    esp_openthread_platform_config_t ot_config = k_openthread_config;
    set_openthread_platform_config(&ot_config);

    /* Per-board setup code (sdkconfig.defaults.unique_setup); nothing to do in the default build */
    setup_code_init();

#if CONFIG_ENABLE_OTA_REQUESTOR
    /* One QueryImage a day by default; the download itself is refused on a weak battery (kOtaStateChanged) */
    esp_matter_ota_config_t ota_config = {};
    ota_config.periodic_query_timeout = CONFIG_APP_OTA_QUERY_INTERVAL_H * 3600;
    esp_matter_ota_requestor_set_config(ota_config);
#endif

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    setup_code_preload();

    /* 802.15.4 TX power, child timeout / supervision, LIT/SIT policy */
    power_thread_setup();

    if (boot_reset) {
        LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { switch_events_factory_reset(); }));
    }

    /* The button: Light-sleep friendly, interrupt + EXT1 wakeup on an LP IO */
    err = button_init(switch_events_on_button);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(err));
    }

    battery_start();

    /* thread_diag runs while a first commissioning window is open (commissioning_state_update) */
    if (identity_diag_start(CONFIG_IDENTITY_DIAG_PERIOD_SEC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the identity diagnostics");
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}
