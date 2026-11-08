// See contact.h. ESP-IDF v6.0.2, ESP32-H2.

#include <sdkconfig.h>

#include "contact.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>

static const char *TAG = "contact";

static constexpr gpio_num_t k_gpio = static_cast<gpio_num_t>(CONFIG_CONTACT_GPIO);
// Above the CHIP task (priority 1) so a long Matter operation does not delay the debounce,
// below the OpenThread task so the radio timing is never disturbed
static constexpr UBaseType_t k_task_priority = 4;
static constexpr uint32_t k_task_stack = 3072;

#if CONFIG_CONTACT_INTERNAL_PULLUP
static constexpr bool k_internal_pullup = true;
#else
static constexpr bool k_internal_pullup = false;
#endif
#if CONFIG_CONTACT_CLOSED_WHEN_LOW
static constexpr bool k_closed_when_low = true;
#else
static constexpr bool k_closed_when_low = false;
#endif

static_assert(SOC_RTCIO_PIN_COUNT > 0, "the contact needs an LP IO EXT1 wakeup source");

static bool s_gpio_ready = false;
static TaskHandle_t s_task = nullptr;
static contact_event_cb_t s_cb = nullptr;
static ContactFsm s_fsm;
static volatile int64_t s_isr_time_us = 0;
static bool s_armed = false;

static bool is_low_raw()
{
    return gpio_get_level(k_gpio) == 0;
}

bool contact_is_closed_raw(void)
{
    return is_low_raw() == k_closed_when_low;
}

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

/* Level interrupt: switch itself off (it would fire continuously) and let the task debounce */
static void contact_isr(void *arg)
{
    gpio_intr_disable(k_gpio);
    s_isr_time_us = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

/* Interrupt and wakeup on the level opposite to the debounced one. If the pin is already at that level the interrupt
 * fires at once, so no transition is lost between the debounce sample and this call. */
static void arm(bool stable_low)
{
    const int level = stable_low ? 1 : 0;
    // Sets the interrupt type to the level and the GPIO wakeup (used if the TOP domain stays powered)
    gpio_wakeup_enable(k_gpio, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    // LP IO wakeup, the only one left when the digital peripherals are powered down in Light-sleep
    esp_sleep_enable_ext1_wakeup_io(1ULL << k_gpio, level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
    s_armed = true;
    gpio_intr_enable(k_gpio);
}

/* During the debounce window: no wakeup on this pin, or the chip would wake again at once on a level that is still
 * present and never sleep through the window */
static void disarm()
{
    gpio_intr_disable(k_gpio);
    gpio_wakeup_disable(k_gpio);
    esp_sleep_disable_ext1_wakeup_io(1ULL << k_gpio);
    s_armed = false;
}

static void contact_task(void *)
{
    ContactFsmConfig cfg;
    cfg.debounce_ms = CONFIG_CONTACT_DEBOUNCE_MS;
    cfg.closed_when_low = k_closed_when_low;
    cfg.gesture_changes = CONFIG_CONTACT_GESTURE_CHANGES;
    cfg.gesture_window_ms = CONFIG_CONTACT_GESTURE_WINDOW_MS;

    s_fsm.Init(cfg, is_low_raw(), now_ms());
    arm(s_fsm.StableLow());
    // The state at start: the Boolean State cluster begins with false (open) whatever the door does
    s_cb(ContactEventInfo{ContactEvent::Changed, s_fsm.Closed()}, true);

    for (;;) {
        TickType_t wait = portMAX_DELAY;
        const int64_t deadline = s_fsm.NextDeadline();
        if (deadline != ContactFsm::kNoDeadline) {
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
            ContactEventInfo events[ContactFsm::kMaxEventsPerPoll];
            const size_t n = s_fsm.Poll(now, is_low_raw(), events);
            for (size_t i = 0; i < n; i++) {
                s_cb(events[i], false);
            }
        }

        if (!s_armed && !s_fsm.DebouncePending()) {
            arm(s_fsm.StableLow());
        }
    }
}

esp_err_t contact_gpio_init(void)
{
    if (s_gpio_ready) {
        return ESP_OK;
    }
    if (!esp_sleep_is_valid_wakeup_gpio(k_gpio)) {
        ESP_LOGE(TAG, "GPIO%d cannot wake the chip from Light-sleep: use an LP IO (GPIO7..GPIO14)", (int)k_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << k_gpio;
    io.mode = GPIO_MODE_INPUT;
    // External pull-up by default (stays defined whatever the sleep pad configuration); internal ~45 kOhm option
    io.pull_up_en = k_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
    // Input hysteresis: the RC-filtered opening edge (1 MOhm x 1 nF) crosses the threshold slowly
    io.hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE;
#endif
    const esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
#if CONFIG_CONTACT_INTERNAL_PULLUP && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    // Latch the pad configuration (pull-up) while the IO MUX is powered down
    gpio_hold_en(k_gpio);
#endif
    // The pull-up charges the 1 nF filter capacitor (R7 / C8) in ~1 ms (external 1 MOhm) or ~0.1 ms (internal)
    esp_rom_delay_us(5000);
    s_gpio_ready = true;
    return ESP_OK;
}

esp_err_t contact_init(contact_event_cb_t cb)
{
    esp_err_t err = contact_gpio_init();
    if (err != ESP_OK) {
        return err;
    }
    s_cb = cb;

    // Not IRAM: during a flash operation the interrupt waits, the level is still there afterwards
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_isr_handler_add(k_gpio, contact_isr, nullptr);
    if (err != ESP_OK) {
        return err;
    }
#if !CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    // TOP domain kept powered: the GPIO wakeup (enabled per pin by gpio_wakeup_enable) works as well
    esp_sleep_enable_gpio_wakeup();
#endif

    if (xTaskCreate(contact_task, "contact", k_task_stack, nullptr, k_task_priority, &s_task) != pdPASS) {
        gpio_isr_handler_remove(k_gpio);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Door contact on GPIO%d (%s pull-up, closed = %s level), debounce %d ms, now %s", (int)k_gpio,
             k_internal_pullup ? "internal" : "external", k_closed_when_low ? "low" : "high",
             CONFIG_CONTACT_DEBOUNCE_MS, contact_is_closed_raw() ? "closed" : "open");
    return ESP_OK;
}

void contact_enable_deep_sleep_wakeup(void)
{
#if !CONFIG_CONTACT_INTERNAL_PULLUP
    // Whatever the door does next (opens or closes) wakes the chip into a normal boot
    esp_sleep_enable_ext1_wakeup_io(1ULL << k_gpio, is_low_raw() ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#endif
}
