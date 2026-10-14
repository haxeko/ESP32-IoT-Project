#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <app_reset.h>
#include <common_macros.h>

#include <app_priv.h>
#include <device_info.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

static void open_commissioning_window_if_needed()
{
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() != 0) {
        return;
    }
    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissionMgr.IsCommissioningWindowOpen()) {
        return;
    }
    constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
    /* Wi-Fi credentials are kept, so only advertise over DNS-SD */
    CHIP_ERROR err =
        commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_needed();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return app_driver_identify(type, effect_id);
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        err = app_driver_attribute_update(endpoint_id, cluster_id, attribute_id, val);
    } else if (type == POST_UPDATE) {
        /* The Matter spec requires a new cluster DataVersion whenever an attribute of the cluster changes. esp-matter
         * 1.6 only increases it for writes from a controller and for attribute::update()/report(), not when a cluster
         * server changes its own attributes while running a command (On/Off, Level Control, Color Control all write
         * through emberAfWriteAttribute). Controllers that cache clusters by DataVersion, like Apple Home, then skip
         * the cluster when they resubscribe and keep showing old values: the color slider showed a stale color and
         * the chosen color was never confirmed, so it was not added to the recent colors. POST_UPDATE runs only when
         * the value really changed, before the change is reported. */
        cluster::increase_data_version(cluster::get(endpoint_id, cluster_id));
    }

    return err;
}

/* Neither esp-matter nor the Matter stack initialize NVS. A partition with no free page or written by a newer NVS
 * format cannot be used at all: erase it like the ESP-IDF examples do, so the device starts as new and can be
 * commissioned again instead of running without storage. */
static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition cannot be used (%s), erasing it", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err)));
}

extern "C" void app_main()
{
    init_nvs();

    /* WS2812 x30 on GPIO4. The light keeps working as a Matter device even if the strip fails to init. */
    app_driver_light_init();

    /* BOOT button (GPIO9): short press toggles, hold 5 s then release = factory reset */
    void *button_handle = app_driver_button_init();
    if (button_handle) {
        app_reset_button_register(button_handle);
    }

    /* Manufacturer "Haxeko", Model "Vibe Light", MAC-based serial number (menuconfig -> Device Information) */
    device_info_init();

    node::config_t node_config;
    device_info_set_name(&node_config);
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));
    device_info_add_attributes(node);

    /* Extended Color Light: on/off, brightness, hue/saturation, XY and color temperature */
    extended_color_light::config_t light_config;
    /* Null StartUp* values: after a power cut, restore the last on/off, brightness and color from NVS */
    light_config.on_off.on_off = DEFAULT_POWER;
    light_config.on_off_lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = DEFAULT_BRIGHTNESS;
    /* OnLevel must stay null: with a value, Off drops CurrentLevel to the minimum (and stores it) and
     * On always jumps to OnLevel, so the brightness set before turning off is lost */
    light_config.level_control.on_level = nullptr;
    light_config.level_control_lighting.start_up_current_level = nullptr;
    light_config.color_control.color_mode = chip::to_underlying(ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation);
    light_config.color_control.enhanced_color_mode =
        chip::to_underlying(ColorControl::EnhancedColorModeEnum::kCurrentHueAndCurrentSaturation);
    light_config.color_control_color_temperature.start_up_color_temperature_mireds = nullptr;
    /* White range 6500 K .. 2000 K (the defaults, 1..65279 mireds, are the spec limits, not a real light). The
     * strip has no white LEDs: these whites are mixed from its red, green and blue LEDs (color_from_mireds). */
    light_config.color_control_color_temperature.color_temp_physical_min_mireds = 153;
    light_config.color_control_color_temperature.color_temp_physical_max_mireds = 500;
    light_config.color_control_color_temperature.couple_color_temp_to_level_min_mireds = 153;
    light_config.identify.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kLightOutput);

    /* esp-matter creates every cluster, feature and command the device type requires, Color Control with the XY and
     * Color Temperature features. Hue/Saturation is optional: added because Apple Home sets colors that way. */
    endpoint_t *endpoint = extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create extended color light endpoint"));
    cluster::color_control::feature::hue_saturation::config_t hue_saturation_config;
    hue_saturation_config.current_hue = 0;
    hue_saturation_config.current_saturation = 0;
    esp_err_t err = cluster::color_control::feature::hue_saturation::add(cluster::get(endpoint, ColorControl::Id),
                                                                         &hue_saturation_config);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to add hue/saturation, err:%d", err));

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Light created with endpoint_id %d", light_endpoint_id);

    /* Level and color move in many small steps during a transition: store them in flash only once the value has
     * settled (CONFIG_ESP_MATTER_DEFERRED_ATTR_PERSISTENCE_TIME_MS, 3 s) instead of at every step. OnOff is still
     * stored immediately. */
    const struct {
        uint32_t cluster_id;
        uint32_t attribute_id;
    } deferred[] = {
        {LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id},
        {ColorControl::Id, ColorControl::Attributes::CurrentHue::Id},
        {ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id},
        {ColorControl::Id, ColorControl::Attributes::CurrentX::Id},
        {ColorControl::Id, ColorControl::Attributes::CurrentY::Id},
        {ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id},
    };
    for (const auto &d : deferred) {
        attribute::set_deferred_persistence(attribute::get(light_endpoint_id, d.cluster_id, d.attribute_id));
    }

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Restore the last on/off, brightness and color persisted in NVS */
    app_driver_light_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif
}
