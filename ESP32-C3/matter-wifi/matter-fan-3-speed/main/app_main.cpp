#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <esp_matter.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

#include <buttons.h>
#include <device_info.h>
#include <fan_ctrl.h>
#include <fan_matter.h>
#include <ir_rx.h>
#include <status_led.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace chip::app::Clusters;

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
/* A new OTA image boots as PENDING_VERIFY. It is confirmed once it has been on Wi-Fi with its fabrics for 60 s.
 * An unconfirmed image is rolled back at the next reset (crash, watchdog or power cycle); nothing forces that
 * reset. */
static constexpr uint64_t k_ota_confirm_delay_us = 60 * 1000 * 1000ULL;
static bool s_ota_pending_verify = false; /* CHIP task */
static esp_timer_handle_t s_ota_confirm_timer = nullptr;

/* CHIP task (FabricCount() needs the CHIP lock) */
static void ota_confirm_work(intptr_t)
{
    if (!s_ota_pending_verify) {
        return;
    }
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0 ||
        !chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected()) {
        /* Never confirm with 0 fabrics: after an OTA that means the new image lost its fabric table */
        esp_timer_start_once(s_ota_confirm_timer, k_ota_confirm_delay_us);
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        s_ota_pending_verify = false;
        ESP_LOGI(TAG, "OTA image confirmed");
    }
}

/* esp_timer task: hop to the CHIP task */
static void ota_confirm_cb(void *)
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

/* CHIP task: (re)start the 60 s count while connected with fabrics */
static void ota_start_confirm_count()
{
    if (s_ota_pending_verify && chip::Server::GetInstance().GetFabricTable().FabricCount() > 0 &&
        chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected() && !esp_timer_is_active(s_ota_confirm_timer)) {
        esp_timer_start_once(s_ota_confirm_timer, k_ota_confirm_delay_us);
    }
}
#endif // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

/* Blue blinking while the device can be commissioned. The window status stays "open" from the first advertisement
 * until commissioning completes, fails for good or times out: PASE closes only the advertisement (and posts
 * kCommissioningWindowClosed) while commissioning goes on. */
static void app_pairing_led_update()
{
    status_led_pairing(chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen());
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange:
        ESP_LOGI(TAG, "Wi-Fi connectivity changed");
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        if (event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            ota_start_confirm_count();
        } else if (event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost &&
                   s_ota_confirm_timer) {
            /* The 60 s count restarts on the next connection */
            esp_timer_stop(s_ota_confirm_timer);
        }
#endif
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        /* The window manager may handle this event after us: do not wait for its cleanup */
        status_led_pairing(false);
        break;

    case chip::DeviceLayer::DeviceEventType::kServerReady:
        app_pairing_led_update();
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        ota_start_confirm_count();
#endif
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
            /* Last fabric gone: start over like a new device, with a fresh 15 min BLE commissioning window (Wi-Fi
             * credentials are erased too, so it can join another network). The fan keeps running. The 1 s delay
             * lets the RemoveFabric response go out first. */
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

/* CHIP task: Identify blinks the status LED blue */
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    using chip::app::Clusters::Identify::EffectIdentifierEnum;
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    switch (type) {
    case identification::START:
        status_led_identify(0);
        break;
    case identification::STOP:
        status_led_identify_stop();
        break;
    case identification::EFFECT:
        switch (static_cast<EffectIdentifierEnum>(effect_id)) {
        case EffectIdentifierEnum::kBlink:
            status_led_identify(1);
            break;
        case EffectIdentifierEnum::kOkay:
            status_led_identify(2);
            break;
        case EffectIdentifierEnum::kBreathe:
            status_led_identify(15);
            break;
        case EffectIdentifierEnum::kChannelChange:
            status_led_identify(8);
            break;
        case EffectIdentifierEnum::kFinishEffect:
            /* Complete the blink in progress, then stop */
            status_led_identify_finish();
            break;
        default: /* kStopEffect: as soon as possible */
            status_led_identify_stop();
            break;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Double click: open the commissioning window (pairing), e.g. to add the fan to a second ecosystem */
static void app_open_pairing()
{
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        chip::Server &server = chip::Server::GetInstance();
        chip::CommissioningWindowManager &cwm = server.GetCommissioningWindowManager();
        if (cwm.IsCommissioningWindowOpen()) {
            ESP_LOGI(TAG, "Commissioning window already open");
            return;
        }
        /* Not commissioned yet: BLE + DNS-SD, as at first boot. Commissioned: BLE has been shut down
         * (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING), so advertise on Wi-Fi only. That is enough to add the fan to a
         * second controller (Apple Home, Google Home, ...) with its original QR / pairing code. */
        const bool commissioned = server.GetFabricTable().FabricCount() > 0;
        CHIP_ERROR err = cwm.OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds32(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS),
            commissioned ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                         : chip::CommissioningWindowAdvertisement::kAllSupported);
        if (err == CHIP_NO_ERROR) {
            ESP_LOGI(TAG, "Commissioning window opened for %d s (%s)", CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS,
                     commissioned ? "Wi-Fi/DNS-SD" : "BLE + DNS-SD");
        } else {
            /* e.g. a commissioning (fail-safe) still in progress */
            ESP_LOGW(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        }
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Pairing ignored, Matter stack not ready: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

/* If the Matter stack cannot complete a factory reset (not running), erase what it would erase (Matter configuration
 * and fabrics, esp-matter attributes, Wi-Fi) and restart. The fan state and the learned remote buttons are kept. */
static void factory_reset_fallback_cb(void *)
{
    ESP_LOGE(TAG, "Factory reset did not complete, erasing the Matter and Wi-Fi data");
    static const char *const k_namespaces[] = {"chip-config", "chip-counters", "CHIP_KVS", "esp_matter_kvs",
                                               "nvs.net80211"};
    for (const char *ns : k_namespaces) {
        nvs_handle_t handle;
        if (nvs_open(ns, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    esp_restart();
}

/* Hold 5 s + release (factory reset task). Erases Wi-Fi and the Matter pairings, then restarts. The fan state and
 * the learned remote buttons are kept. */
static void app_factory_reset()
{
    static esp_timer_handle_t s_fallback;
    const esp_timer_create_args_t args = {
        .callback = factory_reset_fallback_cb,
        .name = "reset_fallback",
    };
    if (s_fallback == nullptr && esp_timer_create(&args, &s_fallback) == ESP_OK) {
        esp_timer_start_once(s_fallback, 10 * 1000 * 1000ULL);
    }
    esp_matter::factory_reset();
}

/* An NVS partition that cannot be used (full, or written by a newer ESP-IDF) is erased: like a factory reset */
static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s), erasing it", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
}

extern "C" void app_main()
{
    init_nvs();

    /* Local control first: the fan resumes its state, and the remote and buttons work, before Matter starts
     * (and also when Matter cannot connect) */
    status_led_init();
    fan_ctrl_init();
    ir_rx_init();
    const buttons_actions_t actions = {
        .open_pairing = app_open_pairing,
        .factory_reset = app_factory_reset,
    };
    buttons_init(&actions);

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ota_check_pending_verify();
#endif

    /* Basic Information: Manufacturer, Model, name, hardware version, serial number */
    device_info_init();

    node::config_t node_config;
    device_info_set_name(&node_config);
    /* If Matter cannot start, the fan keeps running from the remote and the buttons: no abort, which would end in a
     * reboot loop with the fan off (crash guard in fan_ctrl_init). */
    /* No attribute callback: Fan Control changes reach the fan through the cluster delegate (fan_matter.cpp) */
    node_t *node = node::create(&node_config, nullptr, app_identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create the Matter node: running without Matter");
        return;
    }
    device_info_add_attributes(node);

    /* Fan device type: 3 speeds, oscillation (Rocking), natural wind (Wind), see fan_matter.h */
    if (fan_matter_create(node) == nullptr) {
        ESP_LOGE(TAG, "Failed to create the fan endpoint: running without Matter");
        return;
    }

    const esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter (%s): running without Matter", esp_err_to_name(err));
        return;
    }

    /* Write the fan state into the Fan Control cluster, then follow controllers */
    fan_matter_start();
}
