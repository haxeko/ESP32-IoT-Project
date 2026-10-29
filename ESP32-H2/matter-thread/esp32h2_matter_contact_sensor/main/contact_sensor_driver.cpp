#include "contact_sensor_driver.h"

#include <inttypes.h>

#include <atomic>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "contact";

static constexpr uint32_t kTaskStackSize = 3072;
// Below OpenThread (5) so the radio stack is never delayed, above the Matter task (1). The task sleeps
// except for a few microseconds per edge.
static constexpr UBaseType_t kTaskPriority = 4;
// A contact that keeps bouncing for longer than this (a vibrating door, a noisy cable) is sampled anyway,
// so the driver cannot stall; the next edge starts a new debounce.
static constexpr uint32_t kMaxSettleMs = 2000;
// Safety net for an edge interrupt that never arrived (e.g. ESD on a long cable): the pin is compared with
// the reported state this often. Normal changes are reported from the interrupt, not from this check.
static constexpr uint32_t kResyncPeriodMs = 30000;
// Bounded wait for a stable initial reading at boot
static constexpr int kInitialReadAttempts = 10;

static contact_sensor_config_t s_config;
static TaskHandle_t s_task;
static std::atomic<contact_state_t> s_state{CONTACT_STATE_CLOSED};

// pdMS_TO_TICKS() rounds down (10 ms is one tick at 100 Hz, and a one-tick wait can end right away):
// round up and add a tick so the wait is never shorter than requested.
static TickType_t ticks_at_least(uint32_t ms)
{
    return (ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS + 1;
}

contact_state_t reed_level_to_contact_state(int level, bool closed_level_low)
{
    const bool level_low = (level == 0);
    return (level_low == closed_level_low) ? CONTACT_STATE_CLOSED : CONTACT_STATE_OPEN;
}

const char *contact_state_to_str(contact_state_t state)
{
    return state == CONTACT_STATE_CLOSED ? "closed" : "open";
}

int contact_sensor_get_level(void)
{
    return gpio_get_level(static_cast<gpio_num_t>(s_config.gpio_num));
}

contact_state_t contact_sensor_get_state(void)
{
    return s_state.load();
}

static contact_state_t read_pin_state()
{
    return reed_level_to_contact_state(contact_sensor_get_level(), s_config.closed_level_low);
}

// Only wakes the driver task: no Matter API, no logging and no debounce in interrupt context.
static void IRAM_ATTR contact_gpio_isr(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void contact_task(void *arg)
{
    const TickType_t debounce = ticks_at_least(s_config.debounce_ms);
    const TickType_t max_settle = pdMS_TO_TICKS(kMaxSettleMs);

    while (true) {
        // Sleep until an edge, or until the periodic check is due
        const bool edge = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kResyncPeriodMs)) > 0;
        if (!edge && read_pin_state() == s_state.load()) {
            continue;
        }

        // Debounce: every new edge restarts the wait, so the pin is read only once it has been quiet for
        // debounce_ms. The task notification count collects the edges in the meantime.
        const TickType_t start = xTaskGetTickCount();
        while (ulTaskNotifyTake(pdTRUE, debounce) > 0) {
            if (xTaskGetTickCount() - start >= max_settle) {
                ESP_LOGW(TAG, "GPIO%d still bouncing after %" PRIu32 " ms, reading it anyway", s_config.gpio_num,
                         kMaxSettleMs);
                break;
            }
        }

        const contact_state_t state = read_pin_state();
        if (state == s_state.load()) {
            // A glitch, or a quick open/close that ended where it started: nothing to report
            continue;
        }
        if (!edge) {
            ESP_LOGW(TAG, "GPIO%d changed without an interrupt, state corrected", s_config.gpio_num);
        }
        // Store before the callback: whoever the callback hands the work to reads the new state
        s_state.store(state);
        ESP_LOGI(TAG, "Contact %s (GPIO%d level %d)", contact_state_to_str(state), s_config.gpio_num,
                 contact_sensor_get_level());
        s_config.on_change(state, s_config.user_data);
    }
}

esp_err_t contact_sensor_init(const contact_sensor_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != nullptr && config->on_change != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "on_change callback is required");
    ESP_RETURN_ON_FALSE(s_task == nullptr, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(config->gpio_num), ESP_ERR_INVALID_ARG, TAG, "GPIO%d is not valid",
                        config->gpio_num);
    s_config = *config;
    const gpio_num_t pin = static_cast<gpio_num_t>(s_config.gpio_num);

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << s_config.gpio_num;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO%d config failed", s_config.gpio_num);

    // Initial state from the pin itself, never an assumed "closed": two equal readings one debounce apart.
    // The pull-up needs a moment after gpio_config() on a long cable, the first wait covers it.
    const TickType_t debounce = ticks_at_least(s_config.debounce_ms);
    contact_state_t state = read_pin_state();
    for (int i = 0; i < kInitialReadAttempts; i++) {
        vTaskDelay(debounce);
        const contact_state_t again = read_pin_state();
        if (again == state) {
            break;
        }
        state = again;
    }
    s_state.store(state);

    BaseType_t created = xTaskCreate(contact_task, "contact", kTaskStackSize, nullptr, kTaskPriority, &s_task);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "cannot create the debounce task");

    // ESP_ERR_INVALID_STATE: the shared GPIO ISR service is already installed (by the reset button driver)
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service: %s",
                        esp_err_to_name(err));
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(pin, contact_gpio_isr, nullptr), TAG, "GPIO%d ISR handler failed",
                        s_config.gpio_num);

    // An edge between the initial reading and the handler registration had no interrupt: check once now
    xTaskNotifyGive(s_task);

    ESP_LOGI(TAG, "Reed switch on GPIO%d (closed = %s, debounce %" PRIu32 " ms): %s", s_config.gpio_num,
             s_config.closed_level_low ? "LOW" : "HIGH", s_config.debounce_ms, contact_state_to_str(state));
    return ESP_OK;
}
