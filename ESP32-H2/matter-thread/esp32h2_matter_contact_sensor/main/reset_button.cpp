#include "reset_button.h"

#include <inttypes.h>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "reset_button";

// The callbacks run on this stack; the factory reset callback erases an NVS namespace.
static constexpr uint32_t kTaskStackSize = 4096;
// Below OpenThread (5) and the contact sensor task (4)
static constexpr UBaseType_t kTaskPriority = 3;
static constexpr uint32_t kDebounceMs = 30;
// While the button is held, its level is checked this often (only then: the task otherwise sleeps)
static constexpr uint32_t kHoldPollMs = 20;

static reset_button_config_t s_config;
static TaskHandle_t s_task;

static TickType_t ticks_at_least(uint32_t ms)
{
    return (ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS + 1;
}

static bool is_pressed()
{
    return gpio_get_level(static_cast<gpio_num_t>(s_config.gpio_num)) == 0;
}

// Released for a full debounce time
static bool is_released_stable()
{
    if (is_pressed()) {
        return false;
    }
    vTaskDelay(ticks_at_least(kDebounceMs));
    return !is_pressed();
}

static void call(reset_button_cb_t cb)
{
    if (cb != nullptr) {
        cb(s_config.user_data);
    }
}

// Only wakes the button task: no reset, no logging in interrupt context.
static void IRAM_ATTR button_gpio_isr(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void button_task(void *arg)
{
    const TickType_t hold = pdMS_TO_TICKS(s_config.hold_ms);
    bool armed = !is_pressed();
    if (!armed) {
        ESP_LOGW(TAG, "Button on GPIO%d is pressed at start-up: release it to arm the factory reset",
                 s_config.gpio_num);
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(ticks_at_least(kDebounceMs));
        if (!is_pressed()) {
            armed = true;
            continue;
        }
        if (!armed) {
            continue;
        }

        ESP_LOGI(TAG, "Button pressed: hold %" PRIu32 " ms for factory reset", s_config.hold_ms);
        call(s_config.on_hold_start);

        const TickType_t start = xTaskGetTickCount();
        bool released = false;
        while (xTaskGetTickCount() - start < hold) {
            vTaskDelay(ticks_at_least(kHoldPollMs));
            if (is_released_stable()) {
                released = true;
                break;
            }
        }
        // The edges seen while timing the hold were handled by the polling above
        (void) ulTaskNotifyTake(pdTRUE, 0);

        if (released) {
            ESP_LOGI(TAG, "Button released after %" PRIu32 " ms: no factory reset",
                     static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount() - start)));
            call(s_config.on_hold_cancel);
            continue;
        }
        ESP_LOGW(TAG, "Button held for %" PRIu32 " ms", s_config.hold_ms);
        // Not armed again until the button has been released
        armed = false;
        s_config.on_hold_complete(s_config.user_data);
    }
}

esp_err_t reset_button_init(const reset_button_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != nullptr && config->on_hold_complete != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "on_hold_complete callback is required");
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
    // Let the pull-up settle before the task reads the start-up level
    vTaskDelay(ticks_at_least(kDebounceMs));

    BaseType_t created = xTaskCreate(button_task, "reset_btn", kTaskStackSize, nullptr, kTaskPriority, &s_task);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "cannot create the button task");

    // ESP_ERR_INVALID_STATE: the shared GPIO ISR service is already installed (by the contact sensor driver)
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service: %s",
                        esp_err_to_name(err));
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(pin, button_gpio_isr, nullptr), TAG, "GPIO%d ISR handler failed",
                        s_config.gpio_num);

    ESP_LOGI(TAG, "Factory reset button on GPIO%d, hold %" PRIu32 " ms", s_config.gpio_num, s_config.hold_ms);
    return ESP_OK;
}
