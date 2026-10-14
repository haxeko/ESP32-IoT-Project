#include <esp_log.h>

#include <app_priv.h>
#include <color_convert.h>
#include <led_bar.h>

#include <esp_matter.h>

#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>

#include <button_gpio.h>
#include <iot_button.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

/* Matter Level Control: values above MaxLevel (254) mean "null" */
static constexpr uint8_t k_max_level = 254;

/* Last color values received from Matter. ColorMode decides which of them is shown; writing a
 * hue/saturation, XY or temperature value also switches to that mode, because the attribute
 * callback for the value can come before the one for ColorMode. */
static struct {
    ColorControl::ColorModeEnum mode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
    uint8_t hue = 0;
    uint8_t sat = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t mireds = 250;
} s_color;

static rgb_t current_rgb()
{
    switch (s_color.mode) {
    case ColorControl::ColorModeEnum::kCurrentXAndCurrentY:
        return color_from_xy(s_color.x, s_color.y);
    case ColorControl::ColorModeEnum::kColorTemperatureMireds:
        return color_from_mireds(s_color.mireds);
    default:
        return color_from_hue_sat(s_color.hue, s_color.sat);
    }
}

static void apply_color()
{
    rgb_t c = current_rgb();
    led_bar_set_rgb(c.r, c.g, c.b);
}

/* Color Control stores the color three ways (hue/saturation, XY, color temperature) and a command only updates
 * the one it uses; the other attributes keep old values. A controller that sets the color one way and reads it
 * back another way (Apple Home) then shows a stale color, and does not add the chosen color to its recent
 * colors. Once the color has settled, write the same color into the other representations too. ColorMode is
 * left as it is and the app callback is not called: the LEDs already show this color. */
static constexpr uint32_t k_color_sync_delay_ms = 300;

/* Matter thread, stack lock held */
static void set_color_attr_quietly(uint32_t attribute_id, esp_matter_attr_val_t val)
{
    attribute_t *attribute = attribute::get(light_endpoint_id, ColorControl::Id, attribute_id);
    if (attribute && attribute::set_val(attribute, &val, false /* no app callback */) == ESP_OK) {
        MatterReportingAttributeChangeCallback(light_endpoint_id, ColorControl::Id, attribute_id);
    }
}

static void color_sync_cb(chip::System::Layer *layer, void *context)
{
    rgb_t c = current_rgb();
    if (s_color.mode != ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation) {
        color_to_hue_sat(c, &s_color.hue, &s_color.sat);
        set_color_attr_quietly(ColorControl::Attributes::CurrentHue::Id, esp_matter_uint8(s_color.hue));
        set_color_attr_quietly(ColorControl::Attributes::CurrentSaturation::Id, esp_matter_uint8(s_color.sat));
    }
    if (s_color.mode != ColorControl::ColorModeEnum::kCurrentXAndCurrentY) {
        color_to_xy(c, &s_color.x, &s_color.y);
        set_color_attr_quietly(ColorControl::Attributes::CurrentX::Id, esp_matter_uint16(s_color.x));
        set_color_attr_quietly(ColorControl::Attributes::CurrentY::Id, esp_matter_uint16(s_color.y));
    }
    ESP_LOGI(TAG, "Color mode %u: hue %u sat %u, xy %u/%u, %u mireds", chip::to_underlying(s_color.mode), s_color.hue,
             s_color.sat, s_color.x, s_color.y, s_color.mireds);
}

/* Matter thread, stack lock held. Restarted on every step, so it runs once after a transition has finished. */
static void schedule_color_sync()
{
    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Milliseconds32(k_color_sync_delay_ms), color_sync_cb, nullptr);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "color sync timer: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void set_color_mode(uint8_t mode)
{
    switch (mode) {
    case chip::to_underlying(ColorControl::ColorModeEnum::kCurrentXAndCurrentY):
    case chip::to_underlying(ColorControl::ColorModeEnum::kColorTemperatureMireds):
        s_color.mode = static_cast<ColorControl::ColorModeEnum>(mode);
        break;
    default:
        s_color.mode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
        break;
    }
}

static void update_color(uint32_t attribute_id, const esp_matter_attr_val_t *val)
{
    switch (attribute_id) {
    case ColorControl::Attributes::ColorMode::Id:
        set_color_mode(val->val.u8);
        break;
    case ColorControl::Attributes::CurrentHue::Id:
        s_color.hue = val->val.u8;
        s_color.mode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
        break;
    case ColorControl::Attributes::CurrentSaturation::Id:
        s_color.sat = val->val.u8;
        s_color.mode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
        break;
    case ColorControl::Attributes::CurrentX::Id:
        s_color.x = val->val.u16;
        s_color.mode = ColorControl::ColorModeEnum::kCurrentXAndCurrentY;
        break;
    case ColorControl::Attributes::CurrentY::Id:
        s_color.y = val->val.u16;
        s_color.mode = ColorControl::ColorModeEnum::kCurrentXAndCurrentY;
        break;
    case ColorControl::Attributes::ColorTemperatureMireds::Id:
        s_color.mireds = val->val.u16;
        s_color.mode = ColorControl::ColorModeEnum::kColorTemperatureMireds;
        break;
    default:
        return;
    }
    apply_color();
    schedule_color_sync();
}

esp_err_t app_driver_attribute_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val)
{
    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;
    }

    switch (cluster_id) {
    case OnOff::Id:
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            led_bar_set_power(val->val.b);
        }
        break;
    case LevelControl::Id:
        /* Brightness 0-100 % = number of lit LEDs; the intensity of each LED stays fixed */
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id && val->val.u8 <= k_max_level) {
            led_bar_set_level(val->val.u8);
        }
        break;
    case ColorControl::Id:
        update_color(attribute_id, val);
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t app_driver_identify(identification::callback_type_t type, uint8_t effect_id)
{
    switch (type) {
    case identification::START:
        led_bar_identify_start(0); /* until IdentifyTime runs out (STOP) */
        break;
    case identification::STOP:
        led_bar_identify_stop();
        break;
    case identification::EFFECT:
        switch (static_cast<Identify::EffectIdentifierEnum>(effect_id)) {
        case Identify::EffectIdentifierEnum::kBlink:
            led_bar_identify_start(1);
            break;
        case Identify::EffectIdentifierEnum::kOkay:
            led_bar_identify_start(2);
            break;
        case Identify::EffectIdentifierEnum::kChannelChange:
            led_bar_identify_start(8);
            break;
        case Identify::EffectIdentifierEnum::kBreathe:
            led_bar_identify_start(15);
            break;
        default: /* kFinishEffect, kStopEffect */
            led_bar_identify_stop();
            break;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Caller holds the Matter stack lock */
static bool read_attr(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    return attribute && attribute::get_val(attribute, val) == ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    /* get_val() reads through the Matter data model, which runs on the Matter thread */
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &val)) {
        s_color.hue = val.val.u8;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &val)) {
        s_color.sat = val.val.u8;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id, &val)) {
        s_color.x = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id, &val)) {
        s_color.y = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, &val)) {
        s_color.mireds = val.val.u16;
    }
    if (read_attr(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id, &val)) {
        set_color_mode(val.val.u8);
    }

    uint8_t level = DEFAULT_BRIGHTNESS;
    if (read_attr(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val) &&
        val.val.u8 <= k_max_level) {
        level = val.val.u8;
    }
    bool on = DEFAULT_POWER;
    if (read_attr(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val)) {
        on = val.val.b;
    }

    rgb_t c = current_rgb();
    led_bar_set_state(on, level, c.r, c.g, c.b);

    /* Devices paired with older firmware may have stale values in the other color representations */
    schedule_color_sync();
    return ESP_OK;
}

esp_err_t app_driver_light_init()
{
    esp_err_t err = led_bar_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void button_toggle_cb(void *arg, void *data)
{
    /* The LED driver mirrors the OnOff attribute, so no data-model read (and no stack lock) is needed here */
    esp_matter_attr_val_t val = esp_matter_bool(!led_bar_get_power());
    ESP_LOGI(TAG, "Button: turn %s", val.val.b ? "on" : "off");
    /* update() takes the stack lock, runs the attribute callback (drives the LEDs), stores and reports the value */
    attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
}

void *app_driver_button_init()
{
#if CONFIG_APP_BUTTON_GPIO >= 0
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {
        .long_press_time = 5000, /* factory reset (app_reset) */
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
        return NULL;
    }
    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, button_toggle_cb, NULL);
    return handle;
#else
    return NULL;
#endif
}
