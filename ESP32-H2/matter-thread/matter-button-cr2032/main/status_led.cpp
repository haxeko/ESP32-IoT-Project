// See status_led.h. ESP-IDF v6.0.2, ESP32-H2.

#include <sdkconfig.h>

#include "status_led.h"

#include <stdint.h>

namespace {

struct Color {
    uint8_t r, g, b;
    bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

constexpr Color k_off = {0, 0, 0};
constexpr Color k_red = {255, 0, 0};
constexpr Color k_orange = {255, 80, 0};
constexpr Color k_yellow = {255, 160, 0};
constexpr Color k_green = {0, 255, 0};
constexpr Color k_blue = {0, 0, 255};
constexpr Color k_white = {255, 255, 255};

struct Pattern {
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t repeat; // 0: until the mode is cleared
    Color color;    // RGB LED only; a single-colour LED just lights
};

constexpr Pattern k_modes[] = {
    {40, 2960, 0, k_blue},   // PairingWindow
    {200, 300, 0, k_white},  // Identify
    {50, 150, 0, k_yellow},  // BootResetWait
    {100, 400, 0, k_blue},   // HoldPairing
    {50, 100, 0, k_red},     // HoldReset
};
constexpr Pattern k_flashes[] = {
    {20, 0, 1, k_green},     // Ack
    {40, 160, 2, k_orange},  // LowBattery
    {150, 150, 3, k_red},    // ResetConfirm
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

#if STATUS_LED_PRESENT

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "status_led";

/* ---------------------------------------------------------------------------------------------------------------
 * Output: output_init() once, output_write() from the esp_timer task only (k_off = dark) */

#if CONFIG_STATUS_LED_WS2812

/* XL-2121RGBC-WS2812B (datasheet): "0" high 0.25 us (< 0.3 us), "1" high 0.85 us (0.62..1 us), bit 1.25 us, GRB, MSB
 * first, latched after >= 80 us low; 12 mA per colour while its 1.5 kHz PWM is on, 0.7 mA when dark. Its supply is
 * switched by a P-MOSFET, so a dark LED draws nothing; the data line idles low, so it never feeds the unpowered LED. */

#include <driver/rmt_tx.h>
#include <esp_rom_sys.h>

static constexpr gpio_num_t k_data_gpio = static_cast<gpio_num_t>(CONFIG_STATUS_LED_WS2812_DATA_GPIO);
static constexpr uint32_t k_rmt_hz = 8 * 1000 * 1000; // 125 ns ticks, an integer divider of the 32 MHz XTAL
static constexpr unsigned k_brightness = CONFIG_STATUS_LED_WS2812_BRIGHTNESS;
#if CONFIG_STATUS_LED_WS2812_POWER_GPIO >= 0
static constexpr gpio_num_t k_power_gpio = static_cast<gpio_num_t>(CONFIG_STATUS_LED_WS2812_POWER_GPIO);
static constexpr int k_power_on_level = CONFIG_STATUS_LED_WS2812_POWER_ON_LEVEL;
static constexpr uint32_t k_power_up_us = 500; // supply and C26 settle, the LED controller resets
#endif

static rmt_channel_handle_t s_rmt = nullptr;
static rmt_encoder_handle_t s_encoder = nullptr;
static bool s_powered = false;

static void set_power(bool on)
{
#if CONFIG_STATUS_LED_WS2812_POWER_GPIO >= 0
    if (on != s_powered) {
        gpio_set_level(k_power_gpio, on ? k_power_on_level : !k_power_on_level);
        if (on) {
            esp_rom_delay_us(k_power_up_us);
        }
    }
#endif
    s_powered = on;
}

static rmt_symbol_word_t bit_symbol(uint16_t high_ticks, uint16_t low_ticks)
{
    rmt_symbol_word_t s = {};
    s.duration0 = high_ticks;
    s.level0 = 1;
    s.duration1 = low_ticks;
    s.level1 = 0;
    return s;
}

static uint8_t scale(uint8_t v)
{
    if (!v) {
        return 0;
    }
    const unsigned s = v * k_brightness / 255;
    return s ? s : 1;
}

static void send(const Color &c)
{
    if (!s_rmt) {
        return;
    }
    const uint8_t grb[3] = {scale(c.g), scale(c.r), scale(c.b)};
    rmt_transmit_config_t tx = {};
    tx.loop_count = 0;
    tx.flags.eot_level = 0;
    // Enabled only around the frame: an enabled RMT channel holds a CPU_FREQ_MAX lock, which keeps the chip awake
    if (rmt_enable(s_rmt) != ESP_OK) {
        return;
    }
    if (rmt_transmit(s_rmt, s_encoder, grb, sizeof(grb), &tx) == ESP_OK) {
        rmt_tx_wait_all_done(s_rmt, 10); // 24 bits: 30 us
    }
    rmt_disable(s_rmt);
}

static void output_init()
{
#if CONFIG_STATUS_LED_WS2812_POWER_GPIO >= 0
    // Supply off (the gate pull-up kept it off since reset)
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << k_power_gpio;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(k_power_gpio, !k_power_on_level);
    gpio_config(&io);
#endif
    rmt_tx_channel_config_t cfg = {};
    cfg.gpio_num = k_data_gpio;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = k_rmt_hz;
    cfg.mem_block_symbols = 48; // one block: 24 bits + end marker
    cfg.trans_queue_depth = 1;
    cfg.flags.init_level = 0;
    cfg.flags.allow_pd = 1; // registers kept by sleep retention: the peripherals still power down in Light-sleep
    rmt_bytes_encoder_config_t enc = {};
    enc.bit0 = bit_symbol(2, 8); // 0.25 us high, 1.0 us low
    enc.bit1 = bit_symbol(7, 3); // 0.875 us high, 0.375 us low
    enc.flags.msb_first = 1;
    if (rmt_new_tx_channel(&cfg, &s_rmt) != ESP_OK) {
        ESP_LOGE(TAG, "No RMT channel for the RGB LED");
        s_rmt = nullptr;
        return;
    }
    if (rmt_new_bytes_encoder(&enc, &s_encoder) != ESP_OK) {
        ESP_LOGE(TAG, "No RMT encoder for the RGB LED");
        rmt_del_channel(s_rmt);
        s_rmt = nullptr;
    }
}

static void output_write(const Color &c)
{
    if (c == k_off) {
#if CONFIG_STATUS_LED_WS2812_POWER_GPIO >= 0
        set_power(false);
#else
        send(k_off); // always powered: black (it still draws its 0.7 mA)
#endif
        return;
    }
    set_power(true);
    send(c);
}

#else // single-colour LED

static constexpr gpio_num_t k_gpio = static_cast<gpio_num_t>(CONFIG_STATUS_LED_GPIO);
static constexpr int k_on_level = CONFIG_STATUS_LED_ACTIVE_LEVEL;

static void output_init()
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << k_gpio;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(k_gpio, !k_on_level);
    gpio_config(&io);
}

static void output_write(const Color &c)
{
    gpio_set_level(k_gpio, c != k_off ? k_on_level : !k_on_level);
}

#endif // CONFIG_STATUS_LED_WS2812

/* ---------------------------------------------------------------------------------------------------------------
 * Pattern player */

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
static Color s_shown = k_off;
static esp_timer_handle_t s_timer = nullptr;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = nullptr;
#endif

static void led_write(const Color &c)
{
    if (c == s_shown) {
        return;
    }
    const bool was_lit = s_shown != k_off;
    const bool lit = c != k_off;
#if CONFIG_PM_ENABLE
    // No Light-sleep while lit: the pad outputs are not kept while the peripherals are powered down
    if (lit && !was_lit && s_pm_lock) {
        esp_pm_lock_acquire(s_pm_lock);
    }
#endif
    output_write(c);
    s_shown = c;
#if CONFIG_PM_ENABLE
    if (!lit && was_lit && s_pm_lock) {
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
        led_write(k_off);
        s_phase = Phase::Start;
        return;
    }

    uint32_t next_ms = 0;
    switch (s_phase) {
    case Phase::Start:
        led_write(p->color);
        s_phase = Phase::On;
        next_ms = p->on_ms;
        break;
    case Phase::On:
        led_write(k_off);
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
    output_init();
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

#endif // STATUS_LED_PRESENT
