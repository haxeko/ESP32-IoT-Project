/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Local UI logic (ported from the ESPHome configuration, without the radar):
// - WS2812 color follows the air quality level of the last PM2.5 value; the
//   LEDs blink white during a Matter Identify.
// - The OLED is on during start-up (until the first PM2.5 value + 5 s, at most
//   120 s + 5 s), during a factory reset and in pairing mode. Otherwise the
//   display button turns it on and off, and that choice is kept in NVS across
//   restarts.
// - It shows PM2.5 alone, or PM2.5 and CO2 side by side (with a CO2 sensor).
//   local_ui_toggle_layout() (4 presses on the main button) switches between
//   them, and that choice is kept in NVS too.

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#include "drivers/button.h"
#include "drivers/i2c_bus.h"
#include "drivers/sh1106.h"
#include "drivers/status_led.h"
#include "local_ui.h"
#include "ui/fonts.h"

static const char *TAG = "local_ui";

#define UI_TASK_STACK               4096
#define UI_TASK_PRIO                4
#define UI_TICK_MS                  500
#define UI_REDRAW_MS                1000
// Retry the OLED init this often while it does not answer.
#define OLED_RETRY_MS               5000
// While on, send the OLED settings again this often: a panel reset by a short
// power loss still acknowledges the frames but stays dark.
#define OLED_SETTINGS_REFRESH_MS    10000
// Send the LED color again this often: a frame corrupted by a delayed RMT
// interrupt (e.g. during a flash write) would otherwise stay until the next
// level change.
#define LED_REFRESH_MS              5000
// Identify blink: half period (on, then off).
#define IDENTIFY_BLINK_MS           500
// Status line in the log, also useful when the monitor is attached late.
#define STATUS_LOG_MS               30000
// Start-up: keep the screen on until the first PM2.5 value (at most this long)...
#define STARTUP_WAIT_MS             120000
// ...and then for this long so the first result can be read.
#define STARTUP_HOLD_MS             5000
#define LED_BRIGHTNESS              (CONFIG_STATUS_LED_BRIGHTNESS / 100.0f)
// Top of the factory reset title: its accents rise above the font ascent.
#define RESET_TITLE_Y               2
// Baseline of the countdown digits (20 px high): centered in rows 16..63.
#define RESET_DIGIT_BASELINE        50
// Same for the pairing code (digits 12 px high).
#define PAIRING_CODE_BASELINE       46
// PM2.5 + CO2 screen: baseline of the values (digits 17 px high), between the
// names and the line above the level...
#define DUAL_VALUE_BASELINE         40
// ...their margin, the width of the space before "PM2.5" and after "CO2"...
#define DUAL_MARGIN                 3
// ...and the smallest space between them.
#define DUAL_MIN_GAP                6

#if CONFIG_OLED_ROTATE_180
#define OLED_ROTATE_180             true
#else
#define OLED_ROTATE_180             false
#endif

#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
static_assert(CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_RX_PIN &&
              CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_PM_SENSOR_UART_TX_PIN &&
              CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_OLED_SDA_PIN &&
              CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_OLED_SCL_PIN &&
              CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_STATUS_LED_GPIO &&
              CONFIG_DISPLAY_BUTTON_GPIO != CONFIG_BUTTON_GPIO,
              "The display button GPIO is already used");
#if CONFIG_IDF_TARGET_ESP32C3
// Pressed during a reset, a strapping pin changes the boot mode.
static_assert(CONFIG_DISPLAY_BUTTON_GPIO != 2 && CONFIG_DISPLAY_BUTTON_GPIO != 8 && CONFIG_DISPLAY_BUTTON_GPIO != 9 &&
              (CONFIG_DISPLAY_BUTTON_GPIO < 11 || CONFIG_DISPLAY_BUTTON_GPIO > 19),
              "Display button: GPIO2/8/9 are strapping pins, GPIO11-17 flash, GPIO18-19 USB");
#endif
#endif

// Settings saved when they change (u8, 0 or 1): display button setting (on
// or off), and the screen with PM2.5 + CO2 (1) or PM2.5 alone (0).
static constexpr char kNvsNamespace[] = "local_ui";
static constexpr char kNvsDisplayOnKey[] = "display_on";
static constexpr char kNvsShowCo2Key[] = "show_co2";

#if CONFIG_CO2_SENSOR_SCD4X
#define CO2_SENSOR                  1
#else
#define CO2_SENSOR                  0
#endif

#if CONFIG_PM_SENSOR_MODEL_SDS011
#define PM_SENSOR_NAME              "SDS011"
#else
#define PM_SENSOR_NAME              "PMS5003"
#endif

#define AQ_LEVEL_COUNT              5

static const char *const k_level_labels[AQ_LEVEL_COUNT] = {
    "TỐT", "KHÁ", "KHÔNG TỐT", "KÉM", "CỰC KỲ KÉM"
};

// Same palette as the Apple Home air quality scale: green, yellow-green,
// yellow, red, purple.
static const float k_level_colors[AQ_LEVEL_COUNT][3] = {
    { 0.0f, 1.0f, 0.2f },
    { 0.7f, 1.0f, 0.0f },
    { 1.0f, 0.8f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
    { 0.6f, 0.0f, 1.0f },
};

// 9x9 "home" icon shown at the top center while connected to Matter.
#define MATTER_ICON_SIZE            9
static const uint8_t k_matter_icon[MATTER_ICON_SIZE * 2] = {
    0x08, 0x00,     // ....#....
    0x14, 0x00,     // ...#.#...
    0x22, 0x00,     // ..#...#..
    0x41, 0x00,     // .#.....#.
    0xC1, 0x80,     // ##.....##
    0x41, 0x00,     // .#.....#.
    0x5D, 0x00,     // .#.###.#.
    0x55, 0x00,     // .#.#.#.#.
    0x7F, 0x00,     // .#######.
};

// What the OLED shows. s_screen is written from any task under s_lock; the UI
// task draws a copy taken under the lock.
typedef struct {
    float pm25;
    int level;                  // -1 = no data yet
    float co2;                  // ppm, NAN = no value
    bool matter_connected;
    local_ui_reset_t reset;
    int reset_seconds;
    bool pairing;
    char pairing_code[24];      // manual pairing code, digits only ("" = unknown)
} screen_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static screen_t s_screen = { .pm25 = NAN, .level = -1, .co2 = NAN, .matter_connected = false,
                             .reset = LOCAL_UI_RESET_NONE, .reset_seconds = 0, .pairing = false,
                             .pairing_code = "" };
static int64_t s_identify_until_ms;     // under s_lock; 0 = not identifying
static uint32_t s_display_presses;      // under s_lock; display button presses not handled yet
static uint32_t s_layout_toggles;       // under s_lock; local_ui_toggle_layout() calls not handled yet
static TaskHandle_t s_task;
static bool s_oled_ok;
static bool s_led_ok;
static bool s_display_on = true;        // display button setting, loaded by local_ui_init()
static bool s_show_co2;                 // PM2.5 + CO2 screen, loaded by local_ui_init()

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

// Smallest free stack of the task with this name since it started, in bytes
// (ESP-IDF counts stacks in bytes); 0 without such a task.
static unsigned stack_min_free(const char *name)
{
    TaskHandle_t task = xTaskGetHandle(name);
    return task != NULL ? (unsigned)uxTaskGetStackHighWaterMark(task) : 0;
}

// PM2.5 thresholds (µg/m³) of the 5 levels.
int local_ui_pm25_level(float pm25)
{
    if (!isfinite(pm25) || pm25 < 0.0f) {
        return -1;      // NAN would otherwise fail every test below and give the worst level
    }
    return pm25 <= 12.0f  ? 0 :
           pm25 <= 35.4f  ? 1 :
           pm25 <= 55.4f  ? 2 :
           pm25 <= 125.4f ? 3 : 4;
}

void local_ui_set_pm25(float pm25)
{
    const int level = local_ui_pm25_level(pm25);
    if (level < 0) {
        return;     // no value: keep showing the last one, as the ESPHome version did
    }
    taskENTER_CRITICAL(&s_lock);
    s_screen.pm25 = pm25;
    s_screen.level = level;
    taskEXIT_CRITICAL(&s_lock);
}

void local_ui_set_co2(float co2_ppm)
{
    taskENTER_CRITICAL(&s_lock);
    s_screen.co2 = (isfinite(co2_ppm) && co2_ppm >= 0.0f) ? co2_ppm : NAN;
    taskEXIT_CRITICAL(&s_lock);
}

void local_ui_toggle_layout(void)
{
#if CO2_SENSOR
    taskENTER_CRITICAL(&s_lock);
    s_layout_toggles++;
    taskEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);    // the UI task switches and saves it
    }
#else
    ESP_LOGI(TAG, "No CO2 sensor: the screen shows PM2.5 only");
#endif
}

void local_ui_set_matter_connected(bool connected)
{
    taskENTER_CRITICAL(&s_lock);
    const bool changed = (s_screen.matter_connected != connected);
    s_screen.matter_connected = connected;
    taskEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "Matter %s", connected ? "connected" : "not connected");
    }
}

void local_ui_set_reset(local_ui_reset_t reset, int seconds_left)
{
    taskENTER_CRITICAL(&s_lock);
    const bool changed = s_screen.reset != reset || s_screen.reset_seconds != seconds_left;
    s_screen.reset = reset;
    s_screen.reset_seconds = seconds_left;
    taskEXIT_CRITICAL(&s_lock);
    if (changed && s_task != NULL) {
        xTaskNotifyGive(s_task);    // redraw now rather than at the next tick
    }
}

void local_ui_set_pairing(const char *code)
{
    char copy[sizeof(s_screen.pairing_code)] = "";
    if (code != NULL) {
        snprintf(copy, sizeof(copy), "%s", code);
    }
    taskENTER_CRITICAL(&s_lock);
    const bool changed = s_screen.pairing != (code != NULL) || strcmp(s_screen.pairing_code, copy) != 0;
    s_screen.pairing = code != NULL;
    memcpy(s_screen.pairing_code, copy, sizeof(copy));
    taskEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "Pairing mode %s", code != NULL ? "on" : "off");
        if (s_task != NULL) {
            xTaskNotifyGive(s_task);    // show it now rather than at the next tick
        }
    }
}

void local_ui_identify(uint32_t duration_ms)
{
    const int64_t until = duration_ms == 0                         ? 0 :
                          duration_ms == LOCAL_UI_IDENTIFY_FOREVER ? INT64_MAX : now_ms() + duration_ms;
    taskENTER_CRITICAL(&s_lock);
    s_identify_until_ms = until;
    taskEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);    // start or stop blinking now
    }
}

static void save_setting(const char *key, bool value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, key, value ? 1 : 0);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save the %s setting: %s", key, esp_err_to_name(err));
    }
}

#if CONFIG_DISPLAY_BUTTON_GPIO >= 0 || CO2_SENSOR
// Setting saved by save_setting(), default_value when never saved.
static bool load_setting(const char *key, bool default_value)
{
    uint8_t value = default_value ? 1 : 0;
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, key, &value);
        nvs_close(handle);
    }
    return value != 0;
}
#endif

#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
// Runs in the esp_timer task. Acts on the press itself (first poll, held_ms =
// 0) rather than on the release, so the screen reacts at once. The UI task
// handles it: it may be busy (LED fade) and owns the display state.
static void display_button_cb(const button_event_t *event, void *user_data)
{
    if (event->type != BUTTON_HELD || event->held_ms != 0) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    s_display_presses++;
    taskEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}
#endif

// Factory reset: count down while the button is held (4..1), then 0 when
// releasing it resets.
static void draw_reset(local_ui_reset_t reset, int seconds_left)
{
    sh1106_draw_text(64, RESET_TITLE_Y, &font_small, TEXT_ALIGN_TOP_CENTER, "ĐẶT LẠI THIẾT BỊ");
    if (reset == LOCAL_UI_RESET_RUNNING) {
        sh1106_draw_text(64, 36, &font_small, TEXT_ALIGN_CENTER, "Đang đặt lại...");
        return;
    }
    char value[8];
    snprintf(value, sizeof(value), "%d", reset == LOCAL_UI_RESET_HOLD ? seconds_left : 0);
    // Digits centered vertically below the title.
    sh1106_draw_text(64, RESET_DIGIT_BASELINE - font_large.ascent, &font_large, TEXT_ALIGN_TOP_CENTER, value);
}

// Pairing mode: the manual pairing code in groups of 4-3-4 digits, as the
// Home app shows it. Only the 11-digit code fits (standard commissioning
// flow, the one of this device): any other is left out.
static void draw_pairing(const char *code)
{
    sh1106_draw_text(64, RESET_TITLE_Y, &font_small, TEXT_ALIGN_TOP_CENTER, "CHỜ GHÉP NỐI");
    if (strlen(code) != 11) {
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%.4s-%.3s-%.4s", code, code + 4, code + 7);
    sh1106_draw_text(64, PAIRING_CODE_BASELINE - font_medium.ascent, &font_medium, TEXT_ALIGN_TOP_CENTER, text);
}

// One value of the PM2.5 + CO2 screen, "--" without a value.
static void format_dual_value(char *text, size_t size, float value)
{
    if (isnan(value)) {
        snprintf(text, size, "--");
    } else {
        snprintf(text, size, "%d", (int)lroundf(value));
    }
}

// PM2.5 and CO2 side by side, each value below its name, with the level of
// PM2.5 at the bottom (the LEDs and Matter also follow PM2.5 only). Both
// values always have the same size: the large font, unless together they do
// not fit (e.g. PM2.5 1000 and CO2 10000), and then both take a smaller one.
static void draw_dual(const screen_t &screen)
{
    char pm25[12];
    char co2[12];
    format_dual_value(pm25, sizeof(pm25), screen.level >= 0 ? screen.pm25 : NAN);
    format_dual_value(co2, sizeof(co2), screen.co2);
    const int room = SH1106_WIDTH - 1 - 2 * DUAL_MARGIN - DUAL_MIN_GAP;
    const font_t *font = &font_value;
    if (sh1106_text_width(font, pm25) + sh1106_text_width(font, co2) > room) {
        font = &font_medium;
    }
    const int y = DUAL_VALUE_BASELINE - font->ascent;

    sh1106_draw_text(0, 0, &font_small, TEXT_ALIGN_TOP_LEFT, " PM2.5");
    sh1106_draw_text(127, 0, &font_small, TEXT_ALIGN_TOP_RIGHT, "CO2 ");
    sh1106_draw_text(DUAL_MARGIN, y, font, TEXT_ALIGN_TOP_LEFT, pm25);
    sh1106_draw_text(SH1106_WIDTH - 1 - DUAL_MARGIN, y, font, TEXT_ALIGN_TOP_RIGHT, co2);
    sh1106_draw_hline(8, 119, 47);
    sh1106_draw_text(64, 50, &font_small, TEXT_ALIGN_TOP_CENTER,
                     screen.level >= 0 ? k_level_labels[screen.level] : "Đang đo...");
}

// Fails when the OLED does not acknowledge the frame.
static esp_err_t draw_screen(const screen_t &screen, bool show_co2)
{
    const float pm25 = screen.pm25;
    const int level = screen.level;
    sh1106_clear();
    if (screen.reset != LOCAL_UI_RESET_NONE) {
        draw_reset(screen.reset, screen.reset_seconds);
        return sh1106_flush();
    }
    if (screen.pairing) {
        draw_pairing(screen.pairing_code);
        return sh1106_flush();
    }
    if (screen.matter_connected) {
        // Centered horizontally, bottom on the baseline of the "PM2.5" header
        // (drawn at y = 0), so the icon spans exactly the height of its capitals.
        sh1106_draw_bitmap((SH1106_WIDTH - MATTER_ICON_SIZE) / 2, font_small.ascent - MATTER_ICON_SIZE,
                           MATTER_ICON_SIZE, MATTER_ICON_SIZE, k_matter_icon);
    }
    if (show_co2) {
        draw_dual(screen);
    } else if (level < 0) {
        // Below the Matter icon row.
        sh1106_draw_text(64, 24, &font_medium, TEXT_ALIGN_CENTER, PM_SENSOR_NAME);
        sh1106_draw_text(64, 47, &font_small, TEXT_ALIGN_CENTER, "Đang đo...");
    } else {
        char value[8];
        snprintf(value, sizeof(value), "%d", (int)lroundf(pm25));
        sh1106_draw_text(0, 0, &font_small, TEXT_ALIGN_TOP_LEFT, " PM2.5");
        sh1106_draw_text(127, 0, &font_small, TEXT_ALIGN_TOP_RIGHT, "ug/m3 ");
        sh1106_draw_text(64, 16, &font_large, TEXT_ALIGN_TOP_CENTER, value);
        sh1106_draw_hline(8, 119, 47);
        sh1106_draw_text(64, 50, &font_small, TEXT_ALIGN_TOP_CENTER, k_level_labels[level]);
    }
    return sh1106_flush();
}

static bool init_oled();

static void local_ui_task(void *arg)
{
    const int64_t boot_ms = now_ms();
    bool startup = true;
    int64_t startup_hold_from = -1;     // set when the start-up hold period begins
    bool display_on = s_display_on;     // display button setting
    bool show_co2 = s_show_co2;         // PM2.5 + CO2 screen
    bool oled_on = s_oled_ok;
    int64_t last_redraw_ms = 0;
    int64_t last_oled_retry_ms = boot_ms;
    int64_t last_status_ms = boot_ms;
    int64_t last_settings_ms = boot_ms;
    int led_level = -1;
    int64_t last_led_ms = boot_ms;
    int64_t identify_since_ms = -1;     // -1 = not identifying
    bool blink_on = false;
    screen_t drawn = {};    // last frame sent, to redraw at once when the reset or pairing state changes
    bool drawn_show_co2 = show_co2;

    esp_task_wdt_add(NULL);     // restart the device if this task ever stops

    while (true) {
        esp_task_wdt_reset();
        const int64_t now = now_ms();

        taskENTER_CRITICAL(&s_lock);
        const screen_t screen = s_screen;
        const bool identifying = s_identify_until_ms > now;
        const uint32_t presses = s_display_presses;
        s_display_presses = 0;
        const uint32_t layout_toggles = s_layout_toggles;
        s_layout_toggles = 0;
        taskEXIT_CRITICAL(&s_lock);
        const float pm25 = screen.pm25;
        const int level = screen.level;
        const bool resetting = screen.reset != LOCAL_UI_RESET_NONE;

        // Start-up phase: wait for the first value (or the timeout), then hold.
        if (startup) {
            if (startup_hold_from < 0 && (level >= 0 || now - boot_ms >= STARTUP_WAIT_MS)) {
                startup_hold_from = now;
            }
            if (startup_hold_from >= 0 && now - startup_hold_from >= STARTUP_HOLD_MS) {
                startup = false;
            }
        }

        // Display button: each press flips what the screen shows (during
        // start-up it is on even when the setting is off) and ends start-up.
        if (presses > 0) {
            const bool was_on = display_on;
            for (uint32_t i = 0; i < presses; i++) {
                display_on = !(startup || display_on);
                startup = false;
            }
            ESP_LOGI(TAG, "Display button: %s", display_on ? "on" : "off");
            if (display_on != was_on) {
                save_setting(kNvsDisplayOnKey, display_on);
            }
        }

        // Main button (4 presses): PM2.5 alone <-> PM2.5 + CO2.
        if (layout_toggles % 2 != 0) {
            show_co2 = !show_co2;
            ESP_LOGI(TAG, "Screen: %s", show_co2 ? "PM2.5 + CO2" : "PM2.5");
            save_setting(kNvsShowCo2Key, show_co2);
        }
        // Redraw at once when what the screen shows changes.
        const bool mode_changed = screen.reset != drawn.reset || screen.reset_seconds != drawn.reset_seconds ||
                                  screen.pairing != drawn.pairing || show_co2 != drawn_show_co2;

        // LEDs: blink white during a Matter Identify, otherwise show the air
        // quality color, faded when the level changes.
        if (s_led_ok && identifying) {
            if (identify_since_ms < 0) {
                identify_since_ms = now;
                blink_on = false;
            }
            const bool on = ((now - identify_since_ms) / IDENTIFY_BLINK_MS) % 2 == 0;
            if (on != blink_on || now - last_led_ms >= LED_REFRESH_MS) {
                blink_on = on;
                const float v = on ? 1.0f : 0.0f;
                status_led_set_now(v, v, v, LED_BRIGHTNESS);
                last_led_ms = now;
            }
        } else if (s_led_ok) {
            if (identify_since_ms >= 0) {
                // Identify over: back to the air quality color, or off without data.
                identify_since_ms = -1;
                led_level = -1;
                status_led_set_now(0.0f, 0.0f, 0.0f, 0.0f);
                last_led_ms = now;
            }
            if (level >= 0 && level != led_level) {
                led_level = level;
                status_led_set(k_level_colors[level][0], k_level_colors[level][1], k_level_colors[level][2],
                               LED_BRIGHTNESS);
                last_led_ms = now;
            } else if (now - last_led_ms >= LED_REFRESH_MS) {
                status_led_refresh();
                last_led_ms = now;
            }
        }

        // The OLED did not answer at boot: keep retrying (late power-up, loose wire...).
        if (!s_oled_ok && now - last_oled_retry_ms >= OLED_RETRY_MS) {
            last_oled_retry_ms = now;
            s_oled_ok = init_oled();
            oled_on = false;    // turned on below if it should be
        }

        // OLED: on during start-up, when the display button setting is on,
        // during a factory reset or in pairing mode.
        if (s_oled_ok) {
            const bool should_be_on = startup || display_on || resetting || screen.pairing;
            esp_err_t err = ESP_OK;
            if (should_be_on != oled_on) {
                oled_on = should_be_on;
                ESP_LOGI(TAG, "Display %s", oled_on ? "ON" : "OFF");
                if (oled_on) {
                    err = draw_screen(screen, show_co2);    // fresh frame before turning on
                    drawn = screen;
                    drawn_show_co2 = show_co2;
                    last_redraw_ms = now;
                    last_settings_ms = now;         // sh1106_set_power(true) sends them
                }
                if (err == ESP_OK) {
                    err = sh1106_set_power(oled_on);
                }
            } else if (oled_on && (mode_changed || now - last_redraw_ms >= UI_REDRAW_MS)) {
                err = draw_screen(screen, show_co2);
                drawn = screen;
                drawn_show_co2 = show_co2;
                last_redraw_ms = now;
                if (err == ESP_OK && now - last_settings_ms >= OLED_SETTINGS_REFRESH_MS) {
                    last_settings_ms = now;
                    err = sh1106_set_power(true);
                }
            }
            if (err != ESP_OK) {
                // Loose wire or the panel lost power (and with it its settings):
                // initialize it again instead of writing to it every second.
                ESP_LOGW(TAG, "OLED write failed (%s), re-initializing", esp_err_to_name(err));
                s_oled_ok = false;
                last_oled_retry_ms = 0;     // first retry at the next tick
            }
        }

        if (now - last_status_ms >= STATUS_LOG_MS) {
            last_status_ms = now;
            ESP_LOGI(TAG, "status: oled=%s screen=%s startup=%d setting=%s led_level=%d pm25=%d matter=%d co2=%d "
                     "show_co2=%d pairing=%d",
                     s_oled_ok ? "ok" : "missing", oled_on ? "on" : "off", startup, display_on ? "on" : "off",
                     led_level, isnan(pm25) ? -1 : (int)lroundf(pm25), screen.matter_connected,
                     isnan(screen.co2) ? -1 : (int)lroundf(screen.co2), show_co2, screen.pairing);
            // Internal RAM: free now / lowest ever / largest block (RAM headroom check).
            ESP_LOGI(TAG, "heap: free=%u min=%u largest=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            // Stack headroom of the tasks this firmware runs on (stack size check).
            ESP_LOGI(TAG, "stack min free: local_ui=%u pm_sensor=%u scd4x=%u CHIP=%u esp_timer=%u sys_evt=%u",
                     stack_min_free("local_ui"), stack_min_free("pm_sensor"), stack_min_free("scd4x"),
                     stack_min_free("CHIP"), stack_min_free("esp_timer"), stack_min_free("sys_evt"));
        }

        // Woken up early by local_ui_set_reset(), local_ui_set_pairing(), local_ui_identify(),
        // local_ui_toggle_layout() and the display button.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UI_TICK_MS));
    }
}

static bool init_oled()
{
    esp_err_t err = sh1106_init(i2c_bus_get(), CONFIG_OLED_I2C_ADDRESS, OLED_ROTATE_180);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s, retrying every %d s", esp_err_to_name(err), OLED_RETRY_MS / 1000);
        return false;
    }
    return true;
}

esp_err_t local_ui_init(void)
{
#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
    s_display_on = load_setting(kNvsDisplayOnKey, true);
#else
    s_display_on = true;    // no button to turn it back on
#endif
#if CO2_SENSOR
    s_show_co2 = load_setting(kNvsShowCo2Key, true);
#else
    s_show_co2 = false;     // no CO2 value to show
#endif

    // Each part is optional: a missing OLED, LED strip or button must not stop the sensor.
    s_oled_ok = init_oled() && draw_screen(s_screen, s_show_co2) == ESP_OK && sh1106_set_power(true) == ESP_OK;

    esp_err_t err;

    err = status_led_init(CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_COUNT);
    s_led_ok = (err == ESP_OK);
    if (!s_led_ok) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    }

    if (xTaskCreate(local_ui_task, "local_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_DISPLAY_BUTTON_GPIO >= 0
    // After the task exists, so that the first press wakes it up.
    err = button_init(CONFIG_DISPLAY_BUTTON_GPIO, display_button_cb, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Display button on GPIO%d, display %s after start-up", CONFIG_DISPLAY_BUTTON_GPIO,
                 s_display_on ? "on" : "off");
    } else {
        ESP_LOGE(TAG, "Display button init failed: %s", esp_err_to_name(err));
    }
#endif
    return ESP_OK;
}
