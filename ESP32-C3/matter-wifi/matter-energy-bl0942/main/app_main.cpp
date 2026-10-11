/*
 * Matter Smart Plug có đo điện - ESP32-C3 + BL0942 (Matter over Wi-Fi)
 *
 * Endpoint 0: Root Node
 * Endpoint 1: On/Off Plug-in Unit (0x010A) + Electrical Sensor (0x0510)
 *             On/Off, Power Topology, Electrical Power Measurement, Electrical Energy Measurement
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <common_macros.h>
#include <app_reset.h>
#include <iot_button.h>
#include <button_gpio.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#include "energy_meter.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

static uint16_t s_plug_endpoint_id;

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

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                /* Đã xoá fabric cuối cùng: mở lại cửa sổ commissioning qua DNS-SD (vẫn còn Wi-Fi) */
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                                            chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
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

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Relay + LED                                                                */
/* ------------------------------------------------------------------------- */

static void plug_output_init()
{
#if CONFIG_RELAY_GPIO >= 0
    gpio_reset_pin((gpio_num_t)CONFIG_RELAY_GPIO);
    gpio_set_direction((gpio_num_t)CONFIG_RELAY_GPIO, GPIO_MODE_OUTPUT);
#endif
#if CONFIG_LED_GPIO >= 0
    gpio_reset_pin((gpio_num_t)CONFIG_LED_GPIO);
    gpio_set_direction((gpio_num_t)CONFIG_LED_GPIO, GPIO_MODE_OUTPUT);
#endif
}

static void plug_output_set(bool on)
{
#if CONFIG_RELAY_GPIO >= 0
#if CONFIG_RELAY_ACTIVE_HIGH
    gpio_set_level((gpio_num_t)CONFIG_RELAY_GPIO, on ? 1 : 0);
#else
    gpio_set_level((gpio_num_t)CONFIG_RELAY_GPIO, on ? 0 : 1);
#endif
#endif
#if CONFIG_LED_GPIO >= 0
#if CONFIG_LED_ACTIVE_HIGH
    gpio_set_level((gpio_num_t)CONFIG_LED_GPIO, on ? 1 : 0);
#else
    gpio_set_level((gpio_num_t)CONFIG_LED_GPIO, on ? 0 : 1);
#endif
#endif
    ESP_LOGI(TAG, "Plug %s", on ? "ON" : "OFF");
}

static bool plug_get_onoff()
{
    attribute_t *attr = attribute::get(s_plug_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attr, &val);
    return val.val.b;
}

/* Bật/tắt relay khi thuộc tính OnOff thay đổi (từ Apple Home, controller khác hoặc nút bấm) */
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == attribute::PRE_UPDATE && endpoint_id == s_plug_endpoint_id && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        plug_output_set(val->val.b);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Nút bấm: nhấn = bật/tắt ổ cắm, giữ 5 s rồi thả = factory reset             */
/* ------------------------------------------------------------------------- */

static void button_toggle_cb(void *arg, void *data)
{
    esp_matter_attr_val_t val = esp_matter_bool(!plug_get_onoff());
    attribute::update(s_plug_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
}

static void button_init()
{
    const button_config_t btn_cfg = {};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = CONFIG_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t handle = nullptr;
    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button");
        return;
    }
    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, button_toggle_cb, NULL);
    app_reset_button_register(handle);
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    plug_output_init();
    button_init();

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* Endpoint 1: ổ cắm On/Off; StartUpOnOff = null -> khôi phục trạng thái trước khi mất điện */
    endpoint::on_off_plug_in_unit::config_t plug_config;
    plug_config.on_off.on_off = false;
    plug_config.on_off_lighting.start_up_on_off = nullptr;
    endpoint_t *plug_ep = endpoint::on_off_plug_in_unit::create(node, &plug_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(plug_ep != nullptr, ESP_LOGE(TAG, "Failed to create plug endpoint"));
    s_plug_endpoint_id = endpoint::get_id(plug_ep);
    ESP_LOGI(TAG, "Plug created with endpoint_id %d", s_plug_endpoint_id);

    /* Đo điện (Electrical Sensor) trên cùng endpoint với ổ cắm */
    err = energy_meter_add_to_endpoint(plug_ep);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to add energy measurement"));

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Đặt relay theo trạng thái OnOff đã lưu */
    plug_output_set(plug_get_onoff());

    err = energy_meter_start();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start energy meter, err:%d", err));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif
}
