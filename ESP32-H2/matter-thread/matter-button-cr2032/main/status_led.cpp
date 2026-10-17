// See status_led.h. ESP-IDF v6.0.2, ESP32-H2.

#include <sdkconfig.h>

#include "status_led.h"

#include <stdint.h>

namespace {

struct Pattern {
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t repeat; // 0: until the mode is cleared
};

constexpr Pattern k_modes[] = {
    {40, 2960, 0}, // PairingWindow
    {200, 300, 0}, // Identify
    {50, 150, 0},  // BootResetWait
    {100, 400, 0}, // HoldPairing
    {50, 100, 0},  // HoldReset
};
constexpr Pattern k_flashes[] = {
    {20, 0, 1},    // Ack
    {40, 160, 2},  // LowBattery
    {150, 150, 3}, // ResetConfirm
};
static_assert(sizeof(k_modes) / sizeof(k_modes[0]) == static_cast<unsigned>(LedMode::Count), "one pattern per mode");
static_assert(sizeof(k_flashes) / sizeof(k_flashes[0]) == static_cast<unsigned>(LedFlash::Count),
              "one pattern per flash");

} // namespace

unsigned status_led_flash_duration_ms(LedFlash flash)
{
    const Pattern &p = k_flashes[static_cast<unsigned>(flash)];
    return p.repeat * (p.on_ms + p.off_ms);
}

#if CONFIG_STATUS_LED_GPIO >= 0

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "status_led";
static constexpr gpio_num_t k_gpio = static_cast<gpio_num_t>(CONFIG_STATUS_LED_GPIO);
static constexpr int k_on_level = CONFIG_STATUS_LED_ACTIVE_LEVEL;

// Requests, written by any task under s_mux
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_modes = 0;  // bit per LedMode
static int s_flash = -1;      // one-shot being played or pending
static bool s_restart = false;

// Player state: esp_timer task only
enum class Phase : uint8_t { Start, On, Off };
static Phase s_phase = Phase::Start;
static uint8_t s_left = 0;
static int s_playing_flash = -1;
static bool s_lit = false;
static esp_timer_handle_t s_timer = nullptr;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = nullptr;
#endif

static void led_write(bool on)
{
    if (on == s_lit) {
        return;
    }
#if CONFIG_PM_ENABLE
    // No Light-sleep while lit: the pad output is not kept while the peripherals are powered down
    if (on && s_pm_lock) {
        esp_pm_lock_acquire(s_pm_lock);
    }
#endif
    gpio_set_level(k_gpio, on ? k_on_level : !k_on_level);
    s_lit = on;
#if CONFIG_PM_ENABLE
    if (!on && s_pm_lock) {
        esp_pm_lock_release(s_pm_lock);
    }
#endif
}

static void timer_cb(void *)
{
    int flash;
    uint32_t modes;
    portENTER_CRITICAL(&s_mux);
    if (s_restart) {
        s_restart = false;
        s_phase = Phase::Start;
        s_playing_flash = -1;
    }
    flash = s_flash;
    modes = s_modes;
    portEXIT_CRITICAL(&s_mux);

    const Pattern *p = nullptr;
    if (flash >= 0) {
        p = &k_flashes[flash];
        if (s_playing_flash != flash) {
            s_playing_flash = flash;
            s_left = p->repeat;
            s_phase = Phase::Start;
        }
    } else {
        s_playing_flash = -1;
        for (int m = static_cast<int>(LedMode::Count) - 1; m >= 0; m--) {
            if (modes & (1u << m)) {
                p = &k_modes[m];
                break;
            }
        }
    }
    if (!p) {
        led_write(false);
        s_phase = Phase::Start;
        return;
    }

    uint32_t next_ms = 0;
    switch (s_phase) {
    case Phase::Start:
        led_write(true);
        s_phase = Phase::On;
        next_ms = p->on_ms;
        break;
    case Phase::On:
        led_write(false);
        s_phase = Phase::Off;
        next_ms = p->off_ms;
        if (flash >= 0 && s_left > 0 && --s_left == 0) {
            // Flash done: back to the persistent mode (if any) after its last dark phase
            portENTER_CRITICAL(&s_mux);
            if (s_flash == flash) {
                s_flash = -1;
            }
            portEXIT_CRITICAL(&s_mux);
            s_playing_flash = -1;
        }
        break;
    case Phase::Off:
        s_phase = Phase::Start;
        next_ms = 0;
        break;
    }
    esp_timer_start_once(s_timer, next_ms * 1000ULL);
}

static void kick()
{
    if (s_timer) {
        esp_timer_stop(s_timer); // ESP_ERR_INVALID_STATE when not running: fine
        esp_timer_start_once(s_timer, 0);
    }
}

void status_led_init(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << k_gpio;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(k_gpio, !k_on_level);
    gpio_config(&io);
#if CONFIG_PM_ENABLE
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "status_led", &s_pm_lock) != ESP_OK) {
        s_pm_lock = nullptr;
    }
#endif
    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "status_led",
        .skip_unhandled_events = false,
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the LED timer");
        s_timer = nullptr;
    }
}

void status_led_set(LedMode mode, bool on)
{
    const uint32_t bit = 1u << static_cast<unsigned>(mode);
    bool changed;
    portENTER_CRITICAL(&s_mux);
    const uint32_t before = s_modes;
    s_modes = on ? (s_modes | bit) : (s_modes & ~bit);
    changed = before != s_modes;
    if (changed && s_flash < 0) {
        s_restart = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (changed) {
        kick();
    }
}

void status_led_flash(LedFlash flash)
{
    portENTER_CRITICAL(&s_mux);
    s_flash = static_cast<int>(flash);
    s_restart = true;
    portEXIT_CRITICAL(&s_mux);
    kick();
}

#else // no status LED

void status_led_init(void) {}
void status_led_set(LedMode, bool) {}
void status_led_flash(LedFlash) {}

#endif // CONFIG_STATUS_LED_GPIO >= 0
