#include <buttons.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <button_gpio.h>
#include <iot_button.h>

#include <fan_ctrl.h>
#include <ir_rx.h>
#include <status_led.h>

static const char *TAG = "buttons";

/* Button callbacks run in the esp_timer task: they only post commands or notify other tasks */

static buttons_actions_t s_actions;

#if CONFIG_APP_BUTTON_GPIO >= 0
static volatile bool s_reset_armed;
static volatile bool s_reset_started;

static void main_click_cb(void *, void *)
{
    if (ir_learn_active()) {
        ir_learn_skip();
        return;
    }
    fan_ctrl_post_type(FAN_CMD_TOGGLE, 0, FAN_SRC_BUTTON);
}

static void main_double_click_cb(void *, void *)
{
    if (ir_learn_active()) {
        ir_learn_finish();
        return;
    }
    ESP_LOGI(TAG, "Pairing requested");
    if (s_actions.open_pairing) {
        s_actions.open_pairing();
    }
}

static void main_triple_click_cb(void *, void *)
{
#if CONFIG_IR_RX_GPIO >= 0
    ir_learn_start();
#else
    ESP_LOGW(TAG, "No IR receiver (CONFIG_IR_RX_GPIO = -1): nothing to learn");
#endif
}

static void factory_reset_task(void *)
{
    /* Red blinking until the device restarts */
    status_led_reset_running();
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGW(TAG, "Factory reset");
    if (s_actions.factory_reset) {
        s_actions.factory_reset();
    }
    vTaskDelete(NULL);
}

static void main_long_press_cb(void *, void *)
{
    if (!s_reset_armed) {
        s_reset_armed = true;
        ESP_LOGW(TAG, "Release the button to factory reset");
        status_led_reset_armed(true);
    }
}

static void main_press_up_cb(void *, void *)
{
    if (s_reset_armed && !s_reset_started) {
        s_reset_started = true;
        /* The LED feedback and the reset run in a task: button callbacks must not block */
        if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the factory reset task, resetting now");
            if (s_actions.factory_reset) {
                s_actions.factory_reset();
            }
        }
    }
}
#endif

#if CONFIG_BUTTON_SPEED_GPIO >= 0
static void speed_press_cb(void *, void *)
{
    fan_ctrl_post_type(FAN_CMD_SPEED_CYCLE, 0, FAN_SRC_BUTTON);
}
#endif

#if CONFIG_BUTTON_SWING_GPIO >= 0
static void swing_press_cb(void *, void *)
{
    fan_ctrl_post_type(FAN_CMD_SWING_TOGGLE, 0, FAN_SRC_BUTTON);
}
#endif

#if CONFIG_APP_BUTTON_GPIO >= 0 || CONFIG_BUTTON_SPEED_GPIO >= 0 || CONFIG_BUTTON_SWING_GPIO >= 0
static button_handle_t create_button(int gpio, const char *name)
{
    const button_config_t config = {
        .long_press_time = 0,  /* CONFIG_BUTTON_LONG_PRESS_TIME_MS (5 s) */
        .short_press_time = 0, /* CONFIG_BUTTON_SHORT_PRESS_TIME_MS */
    };
    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t handle = NULL;
    if (iot_button_new_gpio_device(&config, &gpio_config, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the %s button on GPIO%d", name, gpio);
        return NULL;
    }
    ESP_LOGI(TAG, "%s button on GPIO%d", name, gpio);
    return handle;
}
#endif

void buttons_init(const buttons_actions_t *actions)
{
    if (actions) {
        s_actions = *actions;
    }

#if CONFIG_APP_BUTTON_GPIO < 0 && CONFIG_BUTTON_SPEED_GPIO < 0 && CONFIG_BUTTON_SWING_GPIO < 0
    ESP_LOGW(TAG, "No buttons (menuconfig -> Buttons): pairing and factory reset only from the apps");
#endif
#if CONFIG_APP_BUTTON_GPIO >= 0
    if (button_handle_t main_btn = create_button(CONFIG_APP_BUTTON_GPIO, "Main")) {
        /* Click, double click and triple click are exclusive: the button component reports the number of
         * clicks once the sequence has ended */
        button_event_args_t triple = {};
        triple.multiple_clicks.clicks = 3;
        iot_button_register_cb(main_btn, BUTTON_SINGLE_CLICK, NULL, main_click_cb, NULL);
        iot_button_register_cb(main_btn, BUTTON_DOUBLE_CLICK, NULL, main_double_click_cb, NULL);
        iot_button_register_cb(main_btn, BUTTON_MULTIPLE_CLICK, &triple, main_triple_click_cb, NULL);
        iot_button_register_cb(main_btn, BUTTON_LONG_PRESS_START, NULL, main_long_press_cb, NULL);
        iot_button_register_cb(main_btn, BUTTON_PRESS_UP, NULL, main_press_up_cb, NULL);
    }
#endif
#if CONFIG_BUTTON_SPEED_GPIO >= 0
    /* Every press counts (quick presses step through the speeds), so react on press down, not on clicks */
    if (button_handle_t speed = create_button(CONFIG_BUTTON_SPEED_GPIO, "Speed")) {
        iot_button_register_cb(speed, BUTTON_PRESS_DOWN, NULL, speed_press_cb, NULL);
    }
#endif
#if CONFIG_BUTTON_SWING_GPIO >= 0
    if (button_handle_t swing = create_button(CONFIG_BUTTON_SWING_GPIO, "Oscillation")) {
        iot_button_register_cb(swing, BUTTON_PRESS_DOWN, NULL, swing_press_cb, NULL);
    }
#endif
}
