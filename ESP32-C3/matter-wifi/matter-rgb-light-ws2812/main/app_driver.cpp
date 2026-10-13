#include <esp_log.h>

#include <app_priv.h>
#include <color_convert.h>
#include <led_bar.h>

#include <esp_matter.h>

#include <button_gpio.h>
#include <iot_button.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

/* Last color values received from Matter; the one written most recently decides the color mode */
enum class color_mode_t : uint8_t { kHueSat, kXY, kTemperature };

static color_mode_t s_mode = color_mode_t::kHueSat;
static uint8_t s_hue = 0;
static uint8_t s_sat = 0;
static uint16_t s_x = 0;
static uint16_t s_y = 0;
static uint16_t s_mireds = 250;

static void apply_color()
{
    rgb_t c;
    switch (s_mode) {
    case color_mode_t::kXY:
        c = color_from_xy(s_x, s_y);
        break;
    case color_mode_t::kTemperature:
        c = color_from_mireds(s_mireds);
        break;
    default:
        c = color_from_hue_sat(s_hue, s_sat);
        break;
    }
    led_bar_set_rgb(c.r, c.g, c.b);
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            led_bar_set_power(val->val.b);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            /* Brightness 0-100 % = number of lit LEDs; the intensity of each LED stays fixed */
            led_bar_set_level(val->val.u8);
        }
    } else if (cluster_id == ColorControl::Id) {
        switch (attribute_id) {
        case ColorControl::Attributes::CurrentHue::Id:
            s_hue = val->val.u8;
            s_mode = color_mode_t::kHueSat;
            apply_color();
            break;
        case ColorControl::Attributes::CurrentSaturation::Id:
            s_sat = val->val.u8;
            s_mode = color_mode_t::kHueSat;
            apply_color();
            break;
        case ColorControl::Attributes::CurrentX::Id:
            s_x = val->val.u16;
            s_mode = color_mode_t::kXY;
            apply_color();
            break;
        case ColorControl::Attributes::CurrentY::Id:
            s_y = val->val.u16;
            s_mode = color_mode_t::kXY;
            apply_color();
            break;
        case ColorControl::Attributes::ColorTemperatureMireds::Id:
            s_mireds = val->val.u16;
            s_mode = color_mode_t::kTemperature;
            apply_color();
            break;
        default:
            break;
        }
    }
    return ESP_OK;
}

static bool read_attr(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    return attribute && attribute::get_val(attribute, val) == ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &val)) {
        s_hue = val.val.u8;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &val)) {
        s_sat = val.val.u8;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id, &val)) {
        s_x = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id, &val)) {
        s_y = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, &val)) {
        s_mireds = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id, &val)) {
        switch (val.val.u8) {
        case (uint8_t)ColorControl::ColorModeEnum::kCurrentXAndCurrentY:
            s_mode = color_mode_t::kXY;
            break;
        case (uint8_t)ColorControl::ColorModeEnum::kColorTemperatureMireds:
            s_mode = color_mode_t::kTemperature;
            break;
        default:
            s_mode = color_mode_t::kHueSat;
            break;
        }
    }
    apply_color();

    if (read_attr(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val)) {
        led_bar_set_level(val.val.u8);
    }
    if (read_attr(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val)) {
        led_bar_set_power(val.val.b);
    }
    return ESP_OK;
}

app_driver_handle_t app_driver_light_init()
{
    esp_err_t err = led_bar_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed: %s", esp_err_to_name(err));
    }
    return (app_driver_handle_t)1;
}

static void button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Button: toggle");
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    if (!read_attr(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val)) {
        return;
    }
    val.val.b = !val.val.b;
    /* update() runs the attribute callback (drives the LEDs) and reports the new state to controllers */
    attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
}

static void button_factory_reset_cb(void *arg, void *data)
{
    ESP_LOGW(TAG, "Button held 5 s: factory reset");
    esp_matter::factory_reset();
}

void app_driver_button_init()
{
#if CONFIG_APP_BUTTON_GPIO >= 0
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {
        .long_press_time = 5000,
        .short_press_time = 0,
    };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = CONFIG_APP_BUTTON_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button on GPIO%d", CONFIG_APP_BUTTON_GPIO);
        return;
    }
    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, button_toggle_cb, NULL);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, button_factory_reset_cb, NULL);
#endif
}
