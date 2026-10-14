#include <status_led.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#if CONFIG_STATUS_LED_TYPE_GPIO
#include <driver/gpio.h>
#elif CONFIG_STATUS_LED_TYPE_WS2812
#include <led_strip.h>
#endif

#if CONFIG_STATUS_LED_TYPE_NONE

void status_led_init() {}
bool status_led_present() { return false; }
void status_led_pairing(bool) {}
void status_led_identify(uint16_t) {}
void status_led_identify_stop() {}
void status_led_identify_finish() {}
void status_led_learning(uint8_t) {}
void status_led_flash() {}
void status_led_reset_armed(bool) {}
void status_led_reset_running() {}
void status_led_running(bool) {}

#else

static const char *TAG = "status_led";

namespace {

struct Color {
    uint8_t r, g, b;
    bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b; }
    bool lit() const { return r || g || b; }
};

/* Low intensities: the LED sits next to the user, often at night */
constexpr Color k_off{0, 0, 0};
constexpr Color k_red{40, 0, 0};
constexpr Color k_green{0, 30, 0};
constexpr Color k_blue{0, 0, 40};
constexpr Color k_yellow{32, 20, 0};
constexpr Color k_dim_green{0, 4, 0};

constexpr int64_t k_tick_ms = 50;
constexpr int64_t k_blink_ms = 500;        /* Identify and pairing: 0.5 s on, 0.5 s off */
constexpr int64_t k_learn_blink_ms = 400;  /* IR learning: 0.2 s on, 0.2 s off per counted blink */
constexpr int64_t k_learn_pause_ms = 1000; /* then a pause */
constexpr int64_t k_flash_ms = 150;
constexpr int64_t k_reset_blink_ms = 250;

struct State {
    bool pairing;
    bool identify;
    uint16_t identify_blinks; /* 0 = until stopped */
    int64_t identify_start_ms;
    uint8_t learn_step;
    int64_t learn_start_ms;
    int64_t flash_until_ms;
    bool reset_armed;
    bool reset_running;
    bool running;
};

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
State s_state;
TaskHandle_t s_task;
bool s_ok; /* LED initialized */
Color s_shown{1, 1, 1}; /* force the first write */

#if CONFIG_STATUS_LED_TYPE_WS2812
led_strip_handle_t s_strip;
#endif

} // namespace

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

static void wake()
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

static void write(const Color &c)
{
#if CONFIG_STATUS_LED_TYPE_GPIO
    gpio_set_level((gpio_num_t)CONFIG_STATUS_LED_GPIO,
                   c.lit() ? CONFIG_STATUS_LED_ACTIVE_LEVEL : !CONFIG_STATUS_LED_ACTIVE_LEVEL);
#elif CONFIG_STATUS_LED_TYPE_WS2812
    esp_err_t err = c.lit() ? led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b) : ESP_OK;
    if (err == ESP_OK) {
        err = c.lit() ? led_strip_refresh(s_strip) : led_strip_clear(s_strip);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED update failed: %s", esp_err_to_name(err));
        return;
    }
#endif
    s_shown = c;
}

/* Highest-priority indication at time `now` */
static Color render(int64_t now)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_state.identify && s_state.identify_blinks &&
        now - s_state.identify_start_ms >= s_state.identify_blinks * 2 * k_blink_ms) {
        s_state.identify = false;
    }
    const State st = s_state;
    taskEXIT_CRITICAL(&s_lock);

    if (st.reset_running) {
        return (now / k_reset_blink_ms) % 2 == 0 ? k_red : k_off;
    }
    if (st.reset_armed) {
        return k_red;
    }
    if (now < st.flash_until_ms) {
        return k_green;
    }
    if (st.learn_step) {
        const int64_t counted = st.learn_step * k_learn_blink_ms;
        const int64_t t = (now - st.learn_start_ms) % (counted + k_learn_pause_ms);
        return (t < counted && t % k_learn_blink_ms < k_learn_blink_ms / 2) ? k_yellow : k_off;
    }
    if (st.identify) {
        return (now - st.identify_start_ms) % (2 * k_blink_ms) < k_blink_ms ? k_blue : k_off;
    }
    if (st.pairing) {
        return (now / k_blink_ms) % 2 == 0 ? k_blue : k_off;
    }
    if (st.running) {
        return k_dim_green;
    }
    return k_off;
}

static void status_led_task(void *)
{
    for (;;) {
        const Color c = render(now_ms());
        if (!(c == s_shown)) {
            write(c);
        }
        /* Woken early by any change, so a flash or a new pattern starts at once */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(k_tick_ms));
    }
}

void status_led_init()
{
#if CONFIG_STATUS_LED_TYPE_GPIO
    const gpio_num_t pin = (gpio_num_t)CONFIG_STATUS_LED_GPIO;
    gpio_set_level(pin, !CONFIG_STATUS_LED_ACTIVE_LEVEL);
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d config failed, no status LED", (int)pin);
        return;
    }
#elif CONFIG_STATUS_LED_TYPE_WS2812
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0, /* driver default: one memory block, enough for one LED */
        .flags = {
            .with_dma = false,
        },
    };
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 on GPIO%d init failed, no status LED", CONFIG_STATUS_LED_GPIO);
        s_strip = NULL;
        return;
    }
#endif
    write(k_off);
    if (xTaskCreate(status_led_task, "status_led", 2048, NULL, 2, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the LED task");
        s_task = NULL;
        return;
    }
    s_ok = true;
}

bool status_led_present()
{
    return s_ok;
}

void status_led_pairing(bool pairing)
{
    taskENTER_CRITICAL(&s_lock);
    s_state.pairing = pairing;
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_identify(uint16_t blinks)
{
    taskENTER_CRITICAL(&s_lock);
    s_state.identify = true;
    s_state.identify_blinks = blinks;
    s_state.identify_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_identify_stop()
{
    taskENTER_CRITICAL(&s_lock);
    s_state.identify = false;
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_identify_finish()
{
    const int64_t now = now_ms();
    taskENTER_CRITICAL(&s_lock);
    if (s_state.identify) {
        const int64_t t = now - s_state.identify_start_ms;
        if (t % (2 * k_blink_ms) < k_blink_ms) {
            /* LED on: stop at the end of this blink */
            s_state.identify_blinks = (uint16_t)(t / (2 * k_blink_ms) + 1);
        } else {
            s_state.identify = false;
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_learning(uint8_t step)
{
    taskENTER_CRITICAL(&s_lock);
    s_state.learn_step = step;
    s_state.learn_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_flash()
{
    taskENTER_CRITICAL(&s_lock);
    s_state.flash_until_ms = now_ms() + k_flash_ms;
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_reset_armed(bool armed)
{
    taskENTER_CRITICAL(&s_lock);
    s_state.reset_armed = armed;
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_reset_running()
{
    taskENTER_CRITICAL(&s_lock);
    s_state.reset_running = true;
    taskEXIT_CRITICAL(&s_lock);
    wake();
}

void status_led_running(bool running)
{
#if CONFIG_STATUS_LED_SHOW_RUNNING
    taskENTER_CRITICAL(&s_lock);
    s_state.running = running;
    taskEXIT_CRITICAL(&s_lock);
    wake();
#else
    (void)running;
#endif
}

#endif // CONFIG_STATUS_LED_TYPE_NONE
