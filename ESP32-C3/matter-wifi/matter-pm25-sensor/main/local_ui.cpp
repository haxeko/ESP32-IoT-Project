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
// Start-up: keep the screen on until the first PM2.5 value (at most this long)...
#define STARTUP_WAIT_MS             120000
// ...and then for this long so the first result can be read.
#define STARTUP_HOLD_MS             5000
#define LED_BRIGHTNESS              (CONFIG_STATUS_LED_BRIGHTNESS / 100.0f)

#if CONFIG_OLED_ROTATE_180
#define OLED_ROTATE_180             true
#else
#define OLED_ROTATE_180             false
#endif

#define AQ_LEVEL_COUNT              5

static const char *const k_level_labels[AQ_LEVEL_COUNT] = {
    "RẤT TỐT", "TỐT", "TRUNG BÌNH", "KÉM", "XẤU"
};

// Blue, green, yellow, orange, red.
static const float k_level_colors[AQ_LEVEL_COUNT][3] = {
    { 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f },
    { 1.0f, 1.0f, 0.0f },
    { 1.0f, 0.35f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static float s_pm25 = NAN;
static int s_aq_level = -1;     // -1 = no data yet
static bool s_oled_ok;
static bool s_led_ok;
static bool s_radar_ok;

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

// PM2.5 thresholds of the US EPA 2024 AQI.
static int pm25_to_level(float pm25)
{
    return pm25 <= 9.0f   ? 0 :
           pm25 <= 35.4f  ? 1 :
           pm25 <= 55.4f  ? 2 :
           pm25 <= 125.4f ? 3 : 4;
}

void local_ui_set_pm25(float pm25)
{
    if (!isfinite(pm25) || pm25 < 0) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    s_pm25 = pm25;
    s_aq_level = pm25_to_level(pm25);
    taskEXIT_CRITICAL(&s_lock);
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

static void draw_screen(float pm25, int level)
{
    sh1106_clear();
    if (level < 0) {
        sh1106_draw_text(64, 18, &font_medium, TEXT_ALIGN_CENTER, "PMS5003");
        sh1106_draw_text(64, 42, &font_small, TEXT_ALIGN_CENTER, "Đang đo...");
    } else {
        char value[8];
        snprintf(value, sizeof(value), "%.0f", pm25);
        sh1106_draw_text(0, 0, &font_small, TEXT_ALIGN_TOP_LEFT, " PM2.5");
        sh1106_draw_text(127, 0, &font_small, TEXT_ALIGN_TOP_RIGHT, "ug/m3 ");
        sh1106_draw_text(64, 16, &font_large, TEXT_ALIGN_TOP_CENTER, value);
        sh1106_draw_hline(8, 119, 47);
        sh1106_draw_text(64, 50, &font_small, TEXT_ALIGN_TOP_CENTER, k_level_labels[level]);
    }
    if (sh1106_flush() != ESP_OK) {
        ESP_LOGW(TAG, "OLED write failed");
    }
}

static void local_ui_task(void *arg)
{
    const int64_t boot_ms = now_ms();
    bool startup = true;
    int64_t startup_hold_from = -1;     // set when the start-up hold period begins
    bool presence = false;
    int64_t last_presence_ms = 0;
    bool oled_on = true;
    int64_t last_redraw_ms = 0;
    int led_level = -1;

    while (true) {
        const int64_t now = now_ms();

        taskENTER_CRITICAL(&s_lock);
        const float pm25 = s_pm25;
        const int level = s_aq_level;
        taskEXIT_CRITICAL(&s_lock);

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

        // OLED: on during start-up or while someone is close.
        if (s_oled_ok) {
            const bool should_be_on = startup || presence;
            if (should_be_on != oled_on) {
                oled_on = should_be_on;
                ESP_LOGI(TAG, "Display %s", oled_on ? "ON" : "OFF");
                if (oled_on) {
                    draw_screen(pm25, level);   // draw a fresh frame before turning on
                    last_redraw_ms = now;
                }
                sh1106_set_power(oled_on);
            } else if (oled_on && now - last_redraw_ms >= UI_REDRAW_MS) {
                draw_screen(pm25, level);
                last_redraw_ms = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

esp_err_t local_ui_init(void)
{
    // Each part is optional: a missing OLED or radar must not stop the sensor.
    esp_err_t err = sh1106_init(CONFIG_OLED_SDA_PIN, CONFIG_OLED_SCL_PIN, CONFIG_OLED_I2C_ADDRESS,
                                OLED_ROTATE_180);
    s_oled_ok = (err == ESP_OK);
    if (s_oled_ok) {
        draw_screen(NAN, -1);
        sh1106_set_power(true);
    } else {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(err));
    }

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

    if (xTaskCreate(local_ui_task, "local_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
