// See button.h. ESP-IDF v6.0.2, ESP32-H2.

#include <sdkconfig.h>

#include "button.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>

static const char *TAG = "button";

static constexpr gpio_num_t k_gpio = static_cast<gpio_num_t>(CONFIG_BUTTON_GPIO);
static constexpr int k_active_level = 0; // the button pulls the pin to GND
// Above the CHIP task (priority 1) so a long Matter operation does not delay the gesture timing,
// below the OpenThread task so the radio timing is never disturbed
static constexpr UBaseType_t k_task_priority = 4;
static constexpr uint32_t k_task_stack = 3072;

#if CONFIG_BUTTON_INTERNAL_PULLUP
static constexpr bool k_internal_pullup = true;
#else
static constexpr bool k_internal_pullup = false;
#endif

static_assert(SOC_RTCIO_PIN_COUNT > 0, "the button needs an LP IO EXT1 wakeup source");

static TaskHandle_t s_task = nullptr;
static button_event_cb_t s_cb = nullptr;
static ButtonFsm s_fsm;
static volatile int64_t s_isr_time_us = 0;
static bool s_armed = false;

bool button_is_pressed_raw(void)
{
    return gpio_get_level(k_gpio) == k_active_level;
}

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

/* Level interrupt: switch itself off (it would fire continuously) and let the task debounce */
static void button_isr(void *arg)
{
    gpio_intr_disable(k_gpio);
    s_isr_time_us = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

/* Interrupt and wakeup on the level opposite to the debounced one: press while released, release while
 * pressed. If the pin is already at that level the interrupt fires at once, so no transition is lost
 * between the debounce sample and this call. */
static void arm(bool stable_pressed)
{
    const int level = stable_pressed ? !k_active_level : k_active_level;
    // Sets the interrupt type to the level and the GPIO wakeup (used if the TOP domain stays powered)
    gpio_wakeup_enable(k_gpio, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    // LP IO wakeup, the only one left when the digital peripherals are powered down in Light-sleep
    esp_sleep_enable_ext1_wakeup_io(1ULL << k_gpio, level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
    s_armed = true;
    gpio_intr_enable(k_gpio);
}

/* During the debounce window: no wakeup on this pin, or the chip would wake again at once on a level that
 * is still present and never sleep through the window */
static void disarm()
{
    gpio_intr_disable(k_gpio);
    gpio_wakeup_disable(k_gpio);
    esp_sleep_disable_ext1_wakeup_io(1ULL << k_gpio);
    s_armed = false;
}

static void button_task(void *)
{
    ButtonFsmConfig cfg;
    cfg.debounce_ms = CONFIG_BUTTON_DEBOUNCE_MS;
    cfg.long_press_ms = CONFIG_BUTTON_LONG_PRESS_MS;
#if CONFIG_SWITCH_MULTI_PRESS
    cfg.multi_press_window_ms = CONFIG_BUTTON_MULTI_PRESS_WINDOW_MS;
#else
    cfg.multi_press_window_ms = 0;
#endif
    cfg.pairing_hold_ms = CONFIG_BUTTON_PAIRING_HOLD_MS;
    cfg.reset_hold_ms = CONFIG_BUTTON_RESET_HOLD_MS;
    cfg.stuck_hold_ms = CONFIG_BUTTON_STUCK_HOLD_MS;

    // Held at start (boot-time reset gesture, handled in app_main): its release produces no gesture
    s_fsm.Init(cfg, button_is_pressed_raw(), now_ms());
    arm(s_fsm.StablePressed());

    for (;;) {
        TickType_t wait = portMAX_DELAY;
        const int64_t deadline = s_fsm.NextDeadline();
        if (deadline != ButtonFsm::kNoDeadline) {
            const int64_t delta = deadline - now_ms();
            // +1 tick: a timeout rounded down would wake up before the deadline and spin once more
            wait = delta <= 0 ? 0 : pdMS_TO_TICKS(delta) + 1;
        }
        if (ulTaskNotifyTake(pdTRUE, wait)) {
            disarm();
            s_fsm.OnRawEdge(s_isr_time_us / 1000);
        }

        const int64_t now = now_ms();
        if (s_fsm.NextDeadline() <= now) {
            ButtonEventInfo events[ButtonFsm::kMaxEventsPerPoll];
            size_t n;
            do {
                n = s_fsm.Poll(now, button_is_pressed_raw(), events);
                for (size_t i = 0; i < n; i++) {
                    s_cb(events[i]);
                }
            } while (n > 0 && s_fsm.NextDeadline() <= now);
        }

        if (!s_armed && !s_fsm.DebouncePending()) {
            arm(s_fsm.StablePressed());
        }
    }
}

esp_err_t button_init(button_event_cb_t cb)
{
    if (!esp_sleep_is_valid_wakeup_gpio(k_gpio)) {
        ESP_LOGE(TAG, "GPIO%d cannot wake the chip from Light-sleep: use an LP IO (GPIO7..GPIO14)", (int)k_gpio);
        return ESP_ERR_INVALID_ARG;
    }
    s_cb = cb;

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << k_gpio;
    io.mode = GPIO_MODE_INPUT;
    // External pull-up by default (stays defined whatever the sleep pad configuration); internal ~45 kOhm option
    io.pull_up_en = k_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
    // Input hysteresis: the RC-filtered release edge crosses the threshold slowly
    io.hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE;
#endif
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
#if CONFIG_BUTTON_INTERNAL_PULLUP && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    // Latch the pad configuration (pull-up) while the IO MUX is powered down
    gpio_hold_en(k_gpio);
#endif

    // Not IRAM: during a flash operation the interrupt waits, the level is still there afterwards
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_isr_handler_add(k_gpio, button_isr, nullptr);
    if (err != ESP_OK) {
        return err;
    }
#if !CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    // TOP domain kept powered: the GPIO wakeup (enabled per pin by gpio_wakeup_enable) works as well
    esp_sleep_enable_gpio_wakeup();
#endif

    if (xTaskCreate(button_task, "button", k_task_stack, nullptr, k_task_priority, &s_task) != pdPASS) {
        gpio_isr_handler_remove(k_gpio);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Button on GPIO%d (active low, %s pull-up), long press %d ms, multi-press window %d ms",
             (int)k_gpio, k_internal_pullup ? "internal" : "external", CONFIG_BUTTON_LONG_PRESS_MS,
#if CONFIG_SWITCH_MULTI_PRESS
             CONFIG_BUTTON_MULTI_PRESS_WINDOW_MS
#else
             0
#endif
    );
    return ESP_OK;
}
