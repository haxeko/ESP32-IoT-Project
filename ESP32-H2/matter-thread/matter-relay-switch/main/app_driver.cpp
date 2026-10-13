#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app_priv.h>
#include <common_macros.h>
#include <esp_matter.h>

#include <button_gpio.h>
#include <device.h>
#include <iot_button.h>
#if CONFIG_STATUS_LED_ENABLE
#include <led_driver.h>
#endif

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t relay_endpoint_id;

static constexpr gpio_num_t k_relay_gpio = (gpio_num_t)CONFIG_RELAY_GPIO;

#if CONFIG_STATUS_LED_ENABLE
static led_driver_handle_t s_led_handle = NULL;
#endif

static bool s_factory_reset_armed = false;
static bool s_factory_reset_started = false;

/* Status LED colors (hue): relay ON = green, factory reset = red */
static constexpr uint16_t k_hue_relay_on = 120;
static constexpr uint16_t k_hue_reset = 0;

static void app_driver_led_set(bool on, uint16_t hue)
{
#if CONFIG_STATUS_LED_ENABLE
    if (s_led_handle) {
        led_driver_set_hue(s_led_handle, hue);
        led_driver_set_power(s_led_handle, on);
    }
#endif
}

static esp_err_t app_driver_relay_set_power(bool on)
{
    int level = on ? CONFIG_RELAY_ACTIVE_LEVEL : !CONFIG_RELAY_ACTIVE_LEVEL;
    ESP_LOGI(TAG, "Relay (GPIO%d) -> %s", (int)k_relay_gpio, on ? "ON" : "OFF");
    if (!s_factory_reset_armed) {
        app_driver_led_set(on, k_hue_relay_on);
    }
    return gpio_set_level(k_relay_gpio, level);
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    uint16_t endpoint_id = relay_endpoint_id;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    val.val.b = !val.val.b;
    /* update() runs the attribute callback (drives the relay) and reports the new state to controllers */
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

static void app_driver_factory_reset_task(void *arg)
{
    /* 3 red blinks confirm the reset, then erase all Matter/Thread data and reboot */
    for (int i = 0; i < 3; i++) {
        app_driver_led_set(true, k_hue_reset);
        vTaskDelay(pdMS_TO_TICKS(250));
        app_driver_led_set(false, k_hue_reset);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_LOGI(TAG, "Starting factory reset");
    esp_matter::factory_reset();
    vTaskDelete(NULL);
}

static void app_driver_button_factory_reset_armed_cb(void *arg, void *data)
{
    if (!s_factory_reset_armed) {
        s_factory_reset_armed = true;
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        /* Solid red: release now */
        app_driver_led_set(true, k_hue_reset);
    }
}

static void app_driver_button_factory_reset_release_cb(void *arg, void *data)
{
    if (s_factory_reset_armed && !s_factory_reset_started) {
        s_factory_reset_started = true;
        /* Blink from a task: button callbacks run in the esp_timer task and must not block */
        if (xTaskCreate(app_driver_factory_reset_task, "factory_reset", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create factory reset task, resetting now");
            esp_matter::factory_reset();
        }
    }
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == relay_endpoint_id && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        err = app_driver_relay_set_power(val->val.b);
    }
    return err;
}

esp_err_t app_driver_relay_set_defaults(uint16_t endpoint_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    esp_err_t err = attribute::get_val(attribute, &val);
    if (err != ESP_OK) {
        return err;
    }
    return app_driver_relay_set_power(val.val.b);
}

app_driver_handle_t app_driver_relay_init()
{
    /* Latch the OFF level before switching the pad to output, so the relay never clicks on at boot.
     * gpio_reset_pin() is not used: it enables the pull-up, which would turn an active-high relay on. */
    gpio_set_level(k_relay_gpio, !CONFIG_RELAY_ACTIVE_LEVEL);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << k_relay_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

#if CONFIG_STATUS_LED_ENABLE
    led_driver_config_t led_cfg = led_driver_get_config();
    s_led_handle = led_driver_init(&led_cfg);
    if (s_led_handle) {
        /* Dim green while the relay is ON */
        led_driver_set_hue(s_led_handle, k_hue_relay_on);
        led_driver_set_saturation(s_led_handle, 100);
        led_driver_set_brightness(s_led_handle, 20);
    }
#endif

    app_driver_relay_set_power(false);
    return (app_driver_handle_t)k_relay_gpio;
}

app_driver_handle_t app_driver_button_init()
{
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = button_driver_get_config();

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, app_driver_button_toggle_cb, NULL);
    /* Hold CONFIG_BUTTON_LONG_PRESS_TIME_MS (5 s): LED turns red; release: 3 red blinks, then factory reset */
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, app_driver_button_factory_reset_armed_cb, NULL);
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, app_driver_button_factory_reset_release_cb, NULL);
    return (app_driver_handle_t)handle;
}
