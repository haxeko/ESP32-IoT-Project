#include <relays.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <fan_logic.h>

static const char *TAG = "relays";

/* ---- Build-time pin checks (every GPIO of the project) ---- */

#if CONFIG_STATUS_LED_TYPE_NONE
#define STATUS_LED_PIN -1
#else
#define STATUS_LED_PIN CONFIG_STATUS_LED_GPIO
#endif

namespace {

constexpr int k_used_pins[] = {
    CONFIG_FAN_RELAY_SPEED1_GPIO, CONFIG_FAN_RELAY_SPEED2_GPIO, CONFIG_FAN_RELAY_SPEED3_GPIO,
    CONFIG_FAN_RELAY_SWING_GPIO,  CONFIG_IR_RX_GPIO,            CONFIG_APP_BUTTON_GPIO,
    CONFIG_BUTTON_SPEED_GPIO,     CONFIG_BUTTON_SWING_GPIO,     CONFIG_BUTTON_NATURAL_GPIO,
    STATUS_LED_PIN,
};

constexpr bool pins_distinct()
{
    constexpr size_t n = sizeof(k_used_pins) / sizeof(k_used_pins[0]);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            if (k_used_pins[i] >= 0 && k_used_pins[i] == k_used_pins[j]) {
                return false;
            }
        }
    }
    return true;
}

/* ESP32-C3: GPIO11-17 belong to the SPI flash, GPIO18/19 to the USB port (flashing and logs) */
constexpr bool pins_usable()
{
    for (int pin : k_used_pins) {
        if (pin >= 11 && pin <= 19) {
            return false;
        }
    }
    return true;
}

/* GPIO20/21 are UART0 RX/TX: the log console (and the ROM boot messages on GPIO21) */
constexpr bool pins_off_console()
{
#if CONFIG_ESP_CONSOLE_UART && CONFIG_ESP_CONSOLE_UART_NUM == 0
    for (int pin : k_used_pins) {
        if (pin == 20 || pin == 21) {
            return false;
        }
    }
#endif
    return true;
}

} // namespace

static_assert(pins_distinct(), "Two functions use the same GPIO: check menuconfig -> Fan Relays / IR Receiver / "
                               "Buttons / Status LED");
static_assert(pins_usable(), "GPIO11-17 (SPI flash) and GPIO18/19 (USB) cannot be used: check menuconfig -> Fan "
                             "Relays / IR Receiver / Buttons / Status LED");
static_assert(pins_off_console(), "GPIO20/21 carry the UART0 log console: check menuconfig -> Fan Relays / IR "
                                  "Receiver / Buttons / Status LED (or move the console to USB-Serial-JTAG)");

/* ---- Relays ---- */

namespace {

const gpio_num_t k_speed_gpio[FAN_SPEED_COUNT] = {
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED1_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED2_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED3_GPIO,
};
constexpr gpio_num_t k_swing_gpio = (gpio_num_t)CONFIG_FAN_RELAY_SWING_GPIO;
constexpr int64_t k_dead_time_us = CONFIG_FAN_RELAY_DEAD_TIME_MS * 1000LL;

uint8_t s_phys_speed;        /* speed relay that is closed (0 = none) */
bool s_phys_swing;           /* oscillation relay closed */
int64_t s_speed_released_us; /* when a speed relay was last released */

} // namespace

static int relay_level(bool closed)
{
    return closed ? CONFIG_FAN_RELAY_ACTIVE_LEVEL : !CONFIG_FAN_RELAY_ACTIVE_LEVEL;
}

/* Wait until FAN_RELAY_DEAD_TIME_MS have passed since a speed relay was released. vTaskDelay(n) can return after
 * only n - 1 full ticks, so check the microsecond clock again. Returns true when it had to wait. */
static bool wait_dead_time()
{
    constexpr int64_t tick_us = portTICK_PERIOD_MS * 1000;
    bool waited = false;
    for (;;) {
        const int64_t left_us = s_speed_released_us + k_dead_time_us - esp_timer_get_time();
        if (left_us <= 0) {
            return waited;
        }
        vTaskDelay((TickType_t)((left_us + tick_us - 1) / tick_us));
        waited = true;
    }
}

uint8_t relays_apply(uint8_t speed, bool swing, bool (*cancel_close)(void))
{
    if (speed > FAN_SPEED_COUNT) {
        speed = FAN_SPEED_COUNT;
    }
    const uint8_t old_speed = s_phys_speed;
    const bool old_swing = s_phys_swing;

    if (!swing && s_phys_swing) {
        gpio_set_level(k_swing_gpio, relay_level(false));
        s_phys_swing = false;
    }
    if (speed != s_phys_speed) {
        if (s_phys_speed) {
            gpio_set_level(k_speed_gpio[s_phys_speed - 1], relay_level(false));
            s_speed_released_us = esp_timer_get_time();
            s_phys_speed = 0;
        }
        if (speed) {
            /* Only a command that came in during the dead time cancels the close */
            if (wait_dead_time() && cancel_close != nullptr && cancel_close()) {
                ESP_LOGD(TAG, "Speed %u relay not closed: newer command waiting", speed);
            } else {
                gpio_set_level(k_speed_gpio[speed - 1], relay_level(true));
                s_phys_speed = speed;
            }
        }
    }
    if (swing && !s_phys_swing && s_phys_speed) {
        gpio_set_level(k_swing_gpio, relay_level(true));
        s_phys_swing = true;
    }
    if (s_phys_speed != old_speed || s_phys_swing != old_swing) {
        if (s_phys_speed) {
            ESP_LOGI(TAG, "Relays: speed %u closed, oscillation %s", s_phys_speed, s_phys_swing ? "closed" : "open");
        } else {
            ESP_LOGI(TAG, "Relays: all speed relays open, oscillation %s", s_phys_swing ? "closed" : "open");
        }
    }
    return s_phys_speed;
}

void relays_init()
{
    uint64_t mask = 1ULL << k_swing_gpio;
    for (gpio_num_t pin : k_speed_gpio) {
        mask |= 1ULL << pin;
    }
    /* Latch the open level before enabling the outputs, so no relay clicks at boot */
    for (gpio_num_t pin : k_speed_gpio) {
        gpio_set_level(pin, relay_level(false));
    }
    gpio_set_level(k_swing_gpio, relay_level(false));
    const gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    /* After a software reset a speed relay may have been closed until just now */
    s_speed_released_us = esp_timer_get_time();
}
