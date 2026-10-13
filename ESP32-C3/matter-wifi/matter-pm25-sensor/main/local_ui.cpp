/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Local UI logic (ported from the ESPHome configuration):
// - WS2812 color follows the air quality level of the last PM2.5 value.
// - The OLED is on during start-up (until the first PM2.5 value + 5 s, at most
//   120 s + 5 s), then only while someone is within 1 m of the LD2410 radar.

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <stdio.h>

#include "drivers/ld2410.h"
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
// Status line in the log, also useful when the monitor is attached late.
#define STATUS_LOG_MS               30000
// Start-up: keep the screen on until the first PM2.5 value (at most this long)...
#define STARTUP_WAIT_MS             120000
// ...and then for this long so the first result can be read.
#define STARTUP_HOLD_MS             5000
#define LED_BRIGHTNESS              (CONFIG_STATUS_LED_BRIGHTNESS / 100.0f)
// Top of the factory reset title: its accents rise above the font ascent.
#define RESET_TITLE_Y               2

#if CONFIG_OLED_ROTATE_180
#define OLED_ROTATE_180             true
#else
#define OLED_ROTATE_180             false
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
    bool matter_connected;
    local_ui_reset_t reset;
    int reset_seconds;
} screen_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static screen_t s_screen = { .pm25 = NAN, .level = -1, .matter_connected = false,
                             .reset = LOCAL_UI_RESET_NONE, .reset_seconds = 0 };
static TaskHandle_t s_task;
static bool s_oled_ok;
static bool s_led_ok;
static bool s_radar_ok;

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
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

// Someone is within CONFIG_PRESENCE_DISTANCE_CM of the radar, and the radar is
// still sending data. The "detection distance" field is not used because the
// LD2410 often reports 0 cm for it while a target is present.
static bool presence_raw(int64_t now)
{
    ld2410_target_t t;
    if (!ld2410_get_target(&t) || (!t.moving && !t.still)) {
        return false;
    }
    if (now - t.timestamp_ms > CONFIG_RADAR_TIMEOUT_SEC * 1000) {
        return false;
    }
    int distance = INT32_MAX;
    if (t.moving && t.moving_cm > 0 && t.moving_cm < distance) {
        distance = t.moving_cm;
    }
    if (t.still && t.still_cm > 0 && t.still_cm < distance) {
        distance = t.still_cm;
    }
    return distance <= CONFIG_PRESENCE_DISTANCE_CM;
}

// Factory reset: count down while the button is held, then ask to release it.
// The texts avoid the hỏi/ngã tone marks, which look like sắc at 12 px.
static void draw_reset(local_ui_reset_t reset, int seconds_left)
{
    sh1106_draw_text(64, RESET_TITLE_Y, &font_small, TEXT_ALIGN_TOP_CENTER, "ĐẶT LẠI THIẾT BỊ");
    if (reset == LOCAL_UI_RESET_RUNNING) {
        sh1106_draw_text(64, 36, &font_small, TEXT_ALIGN_CENTER, "Đang đặt lại...");
        return;
    }
    char value[8];
    snprintf(value, sizeof(value), "%d", reset == LOCAL_UI_RESET_HOLD ? seconds_left : 0);
    sh1106_draw_text(64, 16, &font_large, TEXT_ALIGN_TOP_CENTER, value);
    sh1106_draw_hline(8, 119, 47);
    sh1106_draw_text(64, 50, &font_small, TEXT_ALIGN_TOP_CENTER,
                     reset == LOCAL_UI_RESET_HOLD ? "Buông nút là dừng" : "Buông nút là đặt lại");
}

// Fails when the OLED does not acknowledge the frame.
static esp_err_t draw_screen(const screen_t &screen)
{
    const float pm25 = screen.pm25;
    const int level = screen.level;
    sh1106_clear();
    if (screen.reset != LOCAL_UI_RESET_NONE) {
        draw_reset(screen.reset, screen.reset_seconds);
        return sh1106_flush();
    }
    if (screen.matter_connected) {
        // Centered horizontally, bottom on the baseline of the "PM2.5" header
        // (drawn at y = 0), so the icon spans exactly the height of its capitals.
        sh1106_draw_bitmap((SH1106_WIDTH - MATTER_ICON_SIZE) / 2, font_small.ascent - MATTER_ICON_SIZE,
                           MATTER_ICON_SIZE, MATTER_ICON_SIZE, k_matter_icon);
    }
    if (level < 0) {
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
    bool presence = false;
    int64_t last_presence_ms = 0;
    bool oled_on = s_oled_ok;
    int64_t last_redraw_ms = 0;
    int64_t last_oled_retry_ms = boot_ms;
    int64_t last_status_ms = boot_ms;
    int led_level = -1;
    screen_t drawn = {};    // last frame sent, to redraw at once when the reset state changes

    while (true) {
        const int64_t now = now_ms();

        taskENTER_CRITICAL(&s_lock);
        const screen_t screen = s_screen;
        taskEXIT_CRITICAL(&s_lock);
        const float pm25 = screen.pm25;
        const int level = screen.level;
        const bool resetting = screen.reset != LOCAL_UI_RESET_NONE;
        const bool reset_changed = screen.reset != drawn.reset || screen.reset_seconds != drawn.reset_seconds;

        // Start-up phase: wait for the first value (or the timeout), then hold.
        if (startup) {
            if (startup_hold_from < 0 && (level >= 0 || now - boot_ms >= STARTUP_WAIT_MS)) {
                startup_hold_from = now;
            }
            if (startup_hold_from >= 0 && now - startup_hold_from >= STARTUP_HOLD_MS) {
                startup = false;
            }
        }

        // Presence within 1 m, with a delayed off.
        if (s_radar_ok && presence_raw(now)) {
            presence = true;
            last_presence_ms = now;
        } else if (presence && now - last_presence_ms >= CONFIG_PRESENCE_HOLD_SEC * 1000) {
            presence = false;
        }

        // Air quality indicator: only changes when the level changes.
        if (s_led_ok && level >= 0 && level != led_level) {
            led_level = level;
            status_led_set(k_level_colors[level][0], k_level_colors[level][1], k_level_colors[level][2],
                           LED_BRIGHTNESS);
        }

        // The OLED did not answer at boot: keep retrying (late power-up, loose wire...).
        if (!s_oled_ok && now - last_oled_retry_ms >= OLED_RETRY_MS) {
            last_oled_retry_ms = now;
            s_oled_ok = init_oled();
            oled_on = false;    // turned on below if it should be
        }

        // OLED: on during start-up, while someone is close, or during a factory reset.
        if (s_oled_ok) {
            const bool should_be_on = startup || presence || resetting;
            esp_err_t err = ESP_OK;
            if (should_be_on != oled_on) {
                oled_on = should_be_on;
                ESP_LOGI(TAG, "Display %s", oled_on ? "ON" : "OFF");
                if (oled_on) {
                    err = draw_screen(screen);      // fresh frame before turning on
                    drawn = screen;
                    last_redraw_ms = now;
                }
                if (err == ESP_OK) {
                    err = sh1106_set_power(oled_on);
                }
            } else if (oled_on && (reset_changed || now - last_redraw_ms >= UI_REDRAW_MS)) {
                err = draw_screen(screen);
                drawn = screen;
                last_redraw_ms = now;
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
            ld2410_target_t t;
            const bool radar_seen = s_radar_ok && ld2410_get_target(&t);
            ESP_LOGI(TAG, "status: oled=%s screen=%s startup=%d presence=%d radar=%s led_level=%d pm25=%d matter=%d",
                     s_oled_ok ? "ok" : "missing", oled_on ? "on" : "off", startup, presence,
                     radar_seen ? (t.moving || t.still ? "target" : "no-target") : "no-data",
                     led_level, isnan(pm25) ? -1 : (int)lroundf(pm25), screen.matter_connected);
            if (radar_seen) {
                ESP_LOGI(TAG, "radar: moving=%d %ucm still=%d %ucm, %lld ms ago", t.moving, t.moving_cm, t.still,
                         t.still_cm, (long long)(now - t.timestamp_ms));
            }
        }

        // Woken up early by local_ui_set_reset().
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UI_TICK_MS));
    }
}

static bool init_oled()
{
    esp_err_t err = sh1106_init(CONFIG_OLED_SDA_PIN, CONFIG_OLED_SCL_PIN, CONFIG_OLED_I2C_ADDRESS,
                                OLED_ROTATE_180);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s, retrying every %d s", esp_err_to_name(err), OLED_RETRY_MS / 1000);
        return false;
    }
    return true;
}

esp_err_t local_ui_init(void)
{
    // Each part is optional: a missing OLED or radar must not stop the sensor.
    s_oled_ok = init_oled() && draw_screen(s_screen) == ESP_OK && sh1106_set_power(true) == ESP_OK;

    esp_err_t err;

    err = status_led_init(CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_COUNT);
    s_led_ok = (err == ESP_OK);
    if (!s_led_ok) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    }

    err = ld2410_init(CONFIG_LD2410_UART_RX_PIN, CONFIG_LD2410_UART_TX_PIN);
    s_radar_ok = (err == ESP_OK);
    if (!s_radar_ok) {
        ESP_LOGE(TAG, "LD2410 init failed: %s", esp_err_to_name(err));
    }

    if (xTaskCreate(local_ui_task, "local_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
