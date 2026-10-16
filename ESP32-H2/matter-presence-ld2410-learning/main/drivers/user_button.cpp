/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "user_button.h"

static const char *TAG = "user_button";

static constexpr uint32_t kPollMs = 20;
static constexpr uint32_t kDebounceSamples = 2;    // a level must hold for 40 ms
static constexpr uint32_t kClickMaxMs = 500;       // longer presses are not clicks
static constexpr uint32_t kClickGapMs = 400;       // release of a click -> press of the next one
static constexpr uint32_t kHoldHintMs = 1000;      // log the long-press hint after this

static int s_gpio;
static user_button_config_t s_config;

// Only touched by the poll callback (esp_timer task). Times wrap after 49 days,
// differences stay correct with unsigned arithmetic.
static uint32_t s_now_ms;
static bool s_pressed;      // debounced state
static uint32_t s_unstable; // consecutive samples that differ from s_pressed
static uint32_t s_press_start_ms;
static uint32_t s_release_ms;
static uint8_t s_clicks;    // short presses in the current sequence
static bool s_hint_logged;
static bool s_long_fired;

// The sequence of clicks is over: 2 is a double click, 3 a triple click.
static void end_sequence()
{
    const uint8_t clicks = s_clicks;
    s_clicks = 0;
    if (clicks == 2) {
        ESP_LOGI(TAG, "Double click");
        if (s_config.on_double_click) {
            s_config.on_double_click();
        }
    } else if (clicks == 3) {
        ESP_LOGI(TAG, "Triple click");
        if (s_config.on_triple_click) {
            s_config.on_triple_click();
        }
    } else if (clicks > 3) {
        ESP_LOGI(TAG, "%u clicks, ignored", (unsigned)clicks);
    }
}

static void on_press()
{
    // A press too long after the previous click starts a new sequence.
    if (s_clicks > 0 && s_now_ms - s_release_ms > kClickGapMs) {
        end_sequence();
    }
    s_press_start_ms = s_now_ms;
    s_hint_logged = false;
    s_long_fired = false;
}

static void on_release()
{
    if (s_long_fired || s_now_ms - s_press_start_ms > kClickMaxMs) {
        s_clicks = 0; // a long press cancels the clicks before it
        return;
    }
    s_release_ms = s_now_ms;
    if (s_clicks < UINT8_MAX) {
        s_clicks++;
    }
}

static void poll_cb(void *arg)
{
    s_now_ms += kPollMs;

    const bool pressed = gpio_get_level((gpio_num_t)s_gpio) == 0;
    if (pressed == s_pressed) {
        s_unstable = 0;
    } else if (++s_unstable >= kDebounceSamples) {
        s_unstable = 0;
        s_pressed = pressed;
        if (pressed) {
            on_press();
        } else {
            on_release();
        }
    }

    if (!s_pressed) {
        // Released for kClickGapMs: no more clicks in this sequence.
        if (s_clicks > 0 && s_now_ms - s_release_ms > kClickGapMs) {
            end_sequence();
        }
        return;
    }
    if (s_long_fired) {
        return;
    }
    const uint32_t held_ms = s_now_ms - s_press_start_ms;
    if (!s_hint_logged && held_ms >= kHoldHintMs) {
        s_hint_logged = true;
        ESP_LOGI(TAG, "Button held, keep holding for %" PRIu32 " s in total to factory reset",
                 s_config.long_press_ms / 1000);
    }
    if (held_ms >= s_config.long_press_ms) {
        s_long_fired = true;
        s_clicks = 0;
        if (s_config.on_long_press) {
            s_config.on_long_press();
        }
    }
}

esp_err_t user_button_init(int gpio, const user_button_config_t *config)
{
    if (gpio < 0) {
        return ESP_OK;
    }
    s_gpio = gpio;
    s_config = *config;

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = poll_cb,
        .name = "user_button",
    };
    err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, kPollMs * 1000);
    }
    return err;
}
