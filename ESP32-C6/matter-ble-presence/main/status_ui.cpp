/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "status_ui.h"

#include "sdkconfig.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#if CONFIG_PRESENCE_STATUS_LED_WS2812
#include <led_strip.h>
#endif

#include "click_detector.h"
#include "matter_device.h"

static const char *TAG = "ui";

static constexpr uint32_t kTickMs = 10;
static constexpr uint32_t kBlinkTicks = 50;   // pairing mode: 500 ms on, 500 ms off
static constexpr uint32_t kConfirmTicks = 30; // reset done: 300 ms on, 300 ms off, three times
static constexpr int kConfirmBlinks = 3;
static constexpr uint32_t kResetMagic = 0x52535421; // "RST!"
static constexpr uint8_t kLevel = 40;               // of 255: the LED is bright and next to the eyes

enum Color : uint8_t { kOff, kBlue, kRed };

// Survives the software restart of the factory reset (not the power-on: checked with the reset reason)
static __NOINIT_ATTR uint32_t s_reset_magic;

static click_detector_t s_detector;
static esp_timer_handle_t s_timer;
static uint32_t s_ticks;
static bool s_holding;   // held past the reset time, not released yet
static bool s_resetting; // released: the reset runs, the chip restarts
static bool s_confirm_pending;
static uint32_t s_confirm_left; // ticks of the three red blinks
static uint32_t s_error_left;   // ticks of a long red: the reset did not remove the pairing
static Color s_color = kOff;
#if CONFIG_PRESENCE_STATUS_LED_WS2812
static led_strip_handle_t s_led;
#endif

static bool read_pressed()
{
    return gpio_get_level(static_cast<gpio_num_t>(CONFIG_PRESENCE_BOOT_GPIO)) == 0;
}

static void set_color(Color c)
{
    if (c == s_color) {
        return;
    }
    s_color = c;
#if CONFIG_PRESENCE_STATUS_LED_WS2812
    if (s_led == nullptr) {
        return;
    }
    if (c == kOff) {
        led_strip_clear(s_led);
        return;
    }
    led_strip_set_pixel(s_led, 0, c == kRed ? kLevel : 0, 0, c == kBlue ? kLevel : 0);
    led_strip_refresh(s_led);
#elif CONFIG_PRESENCE_STATUS_LED_PLAIN
    // One-colour LED: the patterns tell the states apart
#if CONFIG_PRESENCE_STATUS_LED_ACTIVE_HIGH
    const bool on_level = true;
#else
    const bool on_level = false;
#endif
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_PRESENCE_STATUS_LED_GPIO), (c != kOff) == on_level ? 1 : 0);
#endif
}

static void start_factory_reset()
{
    ESP_LOGW(TAG, "BOOT released after %d s: removing the Matter pairing", CONFIG_PRESENCE_FACTORY_RESET_HOLD_SEC);
    status_ui_mark_factory_reset();
    s_resetting = matter_device_factory_reset();
    if (!s_resetting) {
        s_reset_magic = 0;
        s_error_left = 200;
    }
}

// After a factory reset: three red blinks once Matter is up and no longer commissioned
static void check_reset_result()
{
    bool started = false;
    bool commissioned = false;
    bool network_up = false;
    uint8_t fabrics = 0;
    matter_device_get_status(&started, &commissioned, &network_up, &fabrics);
    if (!started) {
        return;
    }
    s_confirm_pending = false;
    if (commissioned) {
        ESP_LOGE(TAG, "Factory reset: the device is still commissioned (%u fabric(s))", fabrics);
        s_error_left = 200;
    } else {
        ESP_LOGI(TAG, "Factory reset done: the Matter pairing is removed, waiting to be paired again");
        s_confirm_left = 2 * kConfirmBlinks * kConfirmTicks;
    }
}

static Color wanted_color()
{
    if (s_holding || s_resetting) {
        return kRed;
    }
    if (s_error_left > 0) {
        s_error_left--;
        return kRed;
    }
    if (s_confirm_left > 0) {
        s_confirm_left--;
        return (s_confirm_left / kConfirmTicks) % 2 == 1 ? kRed : kOff;
    }
    if (matter_device_window_open()) {
        return (s_ticks / kBlinkTicks) % 2 == 0 ? kBlue : kOff;
    }
    return kOff;
}

// esp_timer task: sample, debounce, count, LED. Only hands the work over (the Matter task does it).
static void tick_cb(void *)
{
    s_ticks++;
    switch (click_detector_update(&s_detector, read_pressed(), static_cast<uint32_t>(esp_timer_get_time() / 1000))) {
    case CLICK_EVENT_CLICKS:
        ESP_LOGI(TAG, "BOOT double click: pairing mode");
        matter_device_open_pairing_window();
        break;
    case CLICK_EVENT_HOLD:
        s_holding = true;
        ESP_LOGW(TAG, "BOOT held %d s: release it to remove the Matter pairing (factory reset)",
                 CONFIG_PRESENCE_FACTORY_RESET_HOLD_SEC);
        break;
    case CLICK_EVENT_HOLD_RELEASED:
        s_holding = false;
        start_factory_reset();
        break;
    default:
        break;
    }
    if (s_confirm_pending) {
        check_reset_result();
    }
    set_color(wanted_color());
}

void status_ui_mark_factory_reset(void)
{
    s_reset_magic = kResetMagic;
}

static esp_err_t start_led()
{
#if CONFIG_PRESENCE_STATUS_LED_WS2812
    led_strip_config_t strip = {};
    strip.strip_gpio_num = CONFIG_PRESENCE_STATUS_LED_GPIO;
    strip.max_leds = 1;
    strip.led_model = LED_MODEL_WS2812;
    strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt = {};
    rmt.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt.resolution_hz = 10 * 1000 * 1000;
    const esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_led);
    if (err == ESP_OK) {
        led_strip_clear(s_led);
    }
    return err;
#elif CONFIG_PRESENCE_STATUS_LED_PLAIN
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << CONFIG_PRESENCE_STATUS_LED_GPIO;
    io.mode = GPIO_MODE_OUTPUT;
    const esp_err_t err = gpio_config(&io);
    s_color = kRed; // forces the first write
    set_color(kOff);
    return err;
#else
    return ESP_OK;
#endif
}

esp_err_t status_ui_start(void)
{
    // A software restart with the mark set: the factory reset ran, confirmed once Matter is up
    if (s_reset_magic == kResetMagic && esp_reset_reason() == ESP_RST_SW) {
        s_confirm_pending = true;
    }
    s_reset_magic = 0;

    esp_err_t err = start_led();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Status LED on GPIO%d not started: %s", CONFIG_PRESENCE_STATUS_LED_GPIO, esp_err_to_name(err));
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << CONFIG_PRESENCE_BOOT_GPIO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    const click_detector_config_t cfg = {
        .debounce_ms = CONFIG_PRESENCE_BOOT_DEBOUNCE_MS,
        .gap_ms = CONFIG_PRESENCE_BOOT_CLICK_GAP_MS,
        .max_press_ms = CONFIG_PRESENCE_BOOT_CLICK_MAX_PRESS_MS,
        .clicks_needed = 2,
        .hold_ms = CONFIG_PRESENCE_FACTORY_RESET_HOLD_SEC * 1000u,
    };
    // A button already held now is ignored until released
    click_detector_init(&s_detector, &cfg, read_pressed(), static_cast<uint32_t>(esp_timer_get_time() / 1000));

    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "status_ui",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_timer, kTickMs * 1000);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BOOT on GPIO%d: double click = pairing mode (LED blinks blue), hold %d s = factory reset (LED "
                      "red, three red blinks once done); LED on GPIO%d",
                 CONFIG_PRESENCE_BOOT_GPIO, CONFIG_PRESENCE_FACTORY_RESET_HOLD_SEC, CONFIG_PRESENCE_STATUS_LED_GPIO);
    }
    return err;
}
