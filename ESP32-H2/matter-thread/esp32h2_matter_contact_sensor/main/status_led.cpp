#include "status_led.h"

#include <sdkconfig.h>

#if CONFIG_STATUS_LED_ENABLE

#include <atomic>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "status_led";

static constexpr gpio_num_t kLedGpio = static_cast<gpio_num_t>(CONFIG_STATUS_LED_GPIO);
// Bool Kconfig options are not defined at all when off
#if CONFIG_STATUS_LED_ACTIVE_HIGH
static constexpr uint32_t kOnLevel = 1;
#else
static constexpr uint32_t kOnLevel = 0;
#endif
#if CONFIG_STATUS_LED_SHOW_CONTACT
static constexpr bool kShowContact = true;
#else
static constexpr bool kShowContact = false;
#endif
static constexpr uint64_t kTickUs = 100 * 1000;

static std::atomic<bool> s_resetting{false};
static std::atomic<bool> s_reset_hold{false};
static std::atomic<bool> s_identify{false};
static std::atomic<bool> s_commissioning{false};
static std::atomic<bool> s_contact_open{false};
static esp_timer_handle_t s_timer;
static uint32_t s_tick; // esp_timer task only

static void set_led(bool on)
{
    gpio_set_level(kLedGpio, on ? kOnLevel : !kOnLevel);
}

// esp_timer task, every 100 ms
static void led_tick(void *arg)
{
    s_tick++;
    bool on;
    if (s_resetting) {
        on = true;
    } else if (s_reset_hold) {
        on = s_tick & 1;
    } else if (s_identify) {
        on = (s_tick / 2) & 1;
    } else if (s_commissioning) {
        on = (s_tick / 5) & 1;
    } else {
        on = kShowContact && s_contact_open;
    }
    set_led(on);
}

void status_led_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << CONFIG_STATUS_LED_GPIO;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d config failed: %s", CONFIG_STATUS_LED_GPIO, esp_err_to_name(err));
        return;
    }
    set_led(false);

    const esp_timer_create_args_t args = {
        .callback = led_tick,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "status_led",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_timer, kTickUs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED timer failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Status LED on GPIO%d (on = %s)", CONFIG_STATUS_LED_GPIO, kOnLevel ? "HIGH" : "LOW");
}

void status_led_set_resetting(void)
{
    s_resetting = true;
    // Show it at once: the device restarts shortly after
    set_led(true);
}

void status_led_set_reset_hold(bool held)
{
    s_reset_hold = held;
}

void status_led_set_identify(bool on)
{
    s_identify = on;
}

void status_led_set_commissioning(bool window_open)
{
    s_commissioning = window_open;
}

void status_led_set_contact_open(bool open)
{
    s_contact_open = open;
}

#else // !CONFIG_STATUS_LED_ENABLE

void status_led_init(void) {}
void status_led_set_resetting(void) {}
void status_led_set_reset_hold(bool held) {}
void status_led_set_identify(bool on) {}
void status_led_set_commissioning(bool window_open) {}
void status_led_set_contact_open(bool open) {}

#endif // CONFIG_STATUS_LED_ENABLE
