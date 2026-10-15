#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <common_macros.h>

#include <app_priv.h>
#include <device_info.h>
#include <thread_diag.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/thread.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

static const char *TAG = "app_main";
uint16_t relay_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
/* A new OTA image boots as PENDING_VERIFY. It is confirmed once it has been on the real Thread mesh (child/router,
 * or leader with neighbours) with its fabrics for 60 s. An unconfirmed image is rolled back at the next reset
 * (crash, watchdog or power cycle); nothing forces that reset. */
static constexpr uint64_t k_ota_confirm_delay_us = 60 * 1000 * 1000ULL;
static bool s_ota_pending_verify = false;
static esp_timer_handle_t s_ota_confirm_timer = nullptr;

/* An FTD that hears nobody becomes leader of a one-node partition, and CHIP reports that as Established too.
 * Count only a child/router role, or a leader with at least one neighbour. */
static bool ota_on_real_mesh()
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) {
        return false;
    }
    bool on_mesh = false;
    esp_openthread_lock_acquire(portMAX_DELAY);
    switch (otThreadGetDeviceRole(ot)) {
    case OT_DEVICE_ROLE_CHILD:
    case OT_DEVICE_ROLE_ROUTER:
        on_mesh = true;
        break;
    case OT_DEVICE_ROLE_LEADER: {
        otNeighborInfoIterator it = OT_NEIGHBOR_INFO_ITERATOR_INIT;
        otNeighborInfo info;
        on_mesh = otThreadGetNextNeighborInfo(ot, &it, &info) == OT_ERROR_NONE;
        break;
    }
    default:
        break;
    }
    esp_openthread_lock_release();
    return on_mesh;
}

/* CHIP task (FabricCount() needs the CHIP lock) */
static void ota_confirm_work(intptr_t)
{
    if (!s_ota_pending_verify) {
        return;
    }
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0 || !ota_on_real_mesh()) {
        /* Not on the real mesh yet. A later leader -> child/router merge posts no new connectivity event: poll again.
         * Never confirm with 0 fabrics: after an OTA that means the new image lost its fabric table. */
        esp_timer_start_once(s_ota_confirm_timer, k_ota_confirm_delay_us);
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        s_ota_pending_verify = false;
        ESP_LOGI(TAG, "OTA image confirmed");
    }
}

/* esp_timer task: hop to the CHIP task */
static void ota_confirm_cb(void *arg)
{
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(ota_confirm_work) != CHIP_NO_ERROR) {
        esp_timer_start_once(s_ota_confirm_timer, k_ota_confirm_delay_us);
    }
}

static void ota_check_pending_verify()
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_timer_create_args_t args = {
            .callback = ota_confirm_cb,
            .name = "ota_confirm",
        };
        if (esp_timer_create(&args, &s_ota_confirm_timer) == ESP_OK) {
            s_ota_pending_verify = true;
            ESP_LOGW(TAG, "New OTA image pending verification");
        }
    }
}
#endif // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

/* Blue blinking while the device can be commissioned. The window status stays "open" from the first advertisement
 * until commissioning completes, fails for good or times out: PASE closes only the advertisement (and posts
 * kCommissioningWindowClosed) while commissioning goes on. */
static void app_pairing_led_update()
{
    app_driver_pairing_indicate(chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen());
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity changed");
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        if (s_ota_pending_verify) {
            if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
                if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0 &&
                    !esp_timer_is_active(s_ota_confirm_timer)) {
                    esp_timer_start_once(s_ota_confirm_timer, k_ota_confirm_delay_us);
                }
            } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
                /* The 60 s count restarts on the next attach */
                esp_timer_stop(s_ota_confirm_timer);
            }
        }
#endif
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        thread_diag_stop();
        /* The window manager may handle this event after us: do not wait for its cleanup */
        app_driver_pairing_indicate(false);
        break;

    case chip::DeviceLayer::DeviceEventType::kServerReady:
        /* Already commissioned: no need for the periodic Thread diagnostics */
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
            thread_diag_stop();
        }
        app_pairing_led_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        app_pairing_led_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        app_pairing_led_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        app_pairing_led_update();
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        chip::Server &server = chip::Server::GetInstance();
        /* The window is still open when a failed first commissioning reverts its AddNOC: leave that path alone */
        if (server.GetFabricTable().FabricCount() == 0 &&
            !server.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
            /* Last fabric gone: BLE memory is already released and the old Thread dataset would make joining another
             * network fail, so erase everything and reboot into a fresh 15 min BLE commissioning window.
             * The 1 s delay lets the RemoveFabric response go out first. */
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
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

/* Runs on the CHIP task: Identify commands blink the status LED blue */
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    using chip::app::Clusters::Identify::EffectIdentifierEnum;
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    switch (type) {
    case identification::START:
        app_driver_identify_start(0);
        break;
    case identification::STOP:
        app_driver_identify_stop();
        break;
    case identification::EFFECT:
        switch (static_cast<EffectIdentifierEnum>(effect_id)) {
        case EffectIdentifierEnum::kBlink:
            app_driver_identify_start(1);
            break;
        case EffectIdentifierEnum::kOkay:
            app_driver_identify_start(2);
            break;
        case EffectIdentifierEnum::kBreathe:
            app_driver_identify_start(15);
            break;
        case EffectIdentifierEnum::kChannelChange:
            app_driver_identify_start(8);
            break;
        case EffectIdentifierEnum::kFinishEffect:
            /* Complete the blink in progress, then stop */
            app_driver_identify_finish();
            break;
        default: /* kStopEffect: as soon as possible */
            app_driver_identify_stop();
            break;
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
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Relay on GPIO4 (CONFIG_RELAY_GPIO) first: it needs nothing from NVS and ends the reset pull-up window sooner.
     * BOOT button (GPIO9): click = toggle, hold 5 s = factory reset */
    app_driver_handle_t relay_handle = app_driver_relay_init();
    app_driver_button_init();

    nvs_flash_init();

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ota_check_pending_verify();
#endif

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* On/Off Plug-in Unit device type: shows up as a switch/outlet in Apple Home, Google Home, Alexa, HA */
    on_off_plug_in_unit::config_t plug_config;
    plug_config.on_off.on_off = DEFAULT_POWER;
    /* StartUpOnOff = null: restore the last OnOff state after a power cut (the default 0 would force OFF) */
    plug_config.on_off_lighting.start_up_on_off = nullptr;
#if !CONFIG_STATUS_LED_ENABLE
    /* No LED to blink: do not advertise a visible indicator (never identify by switching the mains load) */
    plug_config.identify.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kNone);
#endif
    endpoint_t *endpoint = on_off_plug_in_unit::create(node, &plug_config, ENDPOINT_FLAG_NONE, relay_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create on/off plug-in unit endpoint"));

    relay_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Relay plug-in unit created with endpoint_id %d", relay_endpoint_id);

    /* Basic Information: Manufacturer, Model, Hardware version, SerialNumber */
    device_info_provider_init();
    /* SerialNumber is optional in Basic Information and root_node does not create it: without the attribute the
     * value from the provider is never visible to controllers. Must be added before esp_matter::start(), which
     * reads the cluster's optional attribute set once. The value is managed internally by the provider. */
    cluster_t *basic_info = cluster::get(endpoint::get(node, chip::kRootEndpointId), BasicInformation::Id);
    ABORT_APP_ON_FAILURE(basic_info != nullptr &&
                         cluster::basic_information::attribute::create_serial_number(basic_info, nullptr, 0) != nullptr,
                         ESP_LOGE(TAG, "Failed to add Basic Information SerialNumber attribute"));

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* OpenThread on the ESP32-H2 native 802.15.4 radio */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Log Thread addresses / SRP state until commissioned (see README, troubleshooting) */
    thread_diag_start();

    /* Apply the persisted OnOff (last state, via StartUpOnOff = null) to the relay */
    app_driver_relay_set_defaults(relay_endpoint_id);
    /* Startup reached the relay restore: the next CPU-only reset may keep the relay level again */
    app_driver_relay_boot_done();

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
