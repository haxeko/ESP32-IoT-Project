#include <fan_ctrl.h>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>

#include <status_led.h>

static const char *TAG = "fan_ctrl";

/* ---- Build-time pin checks ---- */

#if CONFIG_STATUS_LED_TYPE_NONE
#define STATUS_LED_PIN -1
#else
#define STATUS_LED_PIN CONFIG_STATUS_LED_GPIO
#endif

namespace {

constexpr int k_used_pins[] = {
    CONFIG_FAN_RELAY_SPEED1_GPIO, CONFIG_FAN_RELAY_SPEED2_GPIO, CONFIG_FAN_RELAY_SPEED3_GPIO,
    CONFIG_FAN_RELAY_SWING_GPIO,  CONFIG_IR_RX_GPIO,            CONFIG_APP_BUTTON_GPIO,
    CONFIG_BUTTON_SPEED_GPIO,     CONFIG_BUTTON_SWING_GPIO,     STATUS_LED_PIN,
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

} // namespace

static_assert(pins_distinct(), "Two functions use the same GPIO: check menuconfig -> Fan Relays / IR Receiver / "
                               "Buttons / Status LED");
static_assert(pins_usable(), "GPIO11-17 (SPI flash) and GPIO18/19 (USB) cannot be used: check menuconfig -> Fan "
                             "Relays / IR Receiver / Buttons / Status LED");

/* ---- Fan state ---- */

namespace {

struct Msg {
    fan_cmd_t cmd;
    uint8_t src; /* fan_src_t */
};

/* NVS record of the state. Same namespace as the learned IR codes. */
struct StoredState {
    uint8_t version;
    uint8_t speed;
    uint8_t last_speed;
    uint8_t swing;
};
constexpr uint8_t k_state_version = 1;
constexpr const char *k_nvs_namespace = "fan";
constexpr const char *k_nvs_key_state = "state";

const gpio_num_t k_speed_gpio[FAN_SPEED_COUNT] = {
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED1_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED2_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED3_GPIO,
};
constexpr gpio_num_t k_swing_gpio = (gpio_num_t)CONFIG_FAN_RELAY_SWING_GPIO;
constexpr int64_t k_dead_time_us = CONFIG_FAN_RELAY_DEAD_TIME_MS * 1000LL;

/* Store the state once it has not changed for this long (a few quick presses on the remote = one flash write) */
constexpr uint32_t k_save_delay_ms = 3000;

/* A start that did not last this long counts as a crash: the next start keeps the fan off */
constexpr uint64_t k_boot_ok_delay_us = 30 * 1000 * 1000ULL;
constexpr uint32_t k_boot_pending_magic = 0x46414E31; /* "FAN1" */

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
fan_state_t s_state;          /* written by the fan task (and fan_ctrl_init), guarded by s_lock */
fan_state_t s_saved;          /* fan task only: last state written to NVS */
uint8_t s_phys_speed;         /* fan task only: speed relay that is closed (0 = none) */
bool s_phys_swing;            /* fan task only */
int64_t s_speed_released_us;  /* fan task only: when a speed relay was last released */
QueueHandle_t s_queue;
fan_listener_t s_listener;
esp_timer_handle_t s_boot_ok_timer;

/* Survives software resets and panics (not power-on): set while a start is younger than k_boot_ok_delay_us */
RTC_NOINIT_ATTR uint32_t s_boot_pending;

} // namespace

static const char *src_name(uint8_t src)
{
    switch (src) {
    case FAN_SRC_MATTER:
        return "Matter";
    case FAN_SRC_IR:
        return "IR remote";
    case FAN_SRC_BUTTON:
        return "button";
    default:
        return "?";
    }
}

static int relay_level(bool closed)
{
    return closed ? CONFIG_FAN_RELAY_ACTIVE_LEVEL : !CONFIG_FAN_RELAY_ACTIVE_LEVEL;
}

/* Fan task only. Break before make: one speed relay at a time, and FAN_RELAY_DEAD_TIME_MS between releasing one
 * and closing another (also across a quick off -> on), so two motor taps are never on the mains together. */
static void relays_apply(const fan_state_t &st)
{
    const bool swing = fan_state_swing_running(&st);

    /* Oscillation stops first and starts last, never without the fan motor */
    if (!swing && s_phys_swing) {
        gpio_set_level(k_swing_gpio, relay_level(false));
        s_phys_swing = false;
    }
    if (st.speed != s_phys_speed) {
        if (s_phys_speed) {
            gpio_set_level(k_speed_gpio[s_phys_speed - 1], relay_level(false));
            s_speed_released_us = esp_timer_get_time();
            s_phys_speed = 0;
        }
        if (st.speed) {
            const int64_t wait_us = s_speed_released_us + k_dead_time_us - esp_timer_get_time();
            if (wait_us > 0) {
                vTaskDelay(pdMS_TO_TICKS((wait_us + 999) / 1000) + 1);
            }
            gpio_set_level(k_speed_gpio[st.speed - 1], relay_level(true));
            s_phys_speed = st.speed;
        }
    }
    if (swing && !s_phys_swing) {
        gpio_set_level(k_swing_gpio, relay_level(true));
        s_phys_swing = true;
    }
}

static void relays_init()
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

static bool load_state(fan_state_t *st)
{
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &handle) != ESP_OK) {
        return false; /* nothing stored yet */
    }
    StoredState stored;
    size_t len = sizeof(stored);
    const esp_err_t err = nvs_get_blob(handle, k_nvs_key_state, &stored, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(stored) || stored.version != k_state_version) {
        return false;
    }
    st->speed = stored.speed;
    st->last_speed = stored.last_speed;
    st->swing = stored.swing != 0;
    fan_state_sanitize(st);
    return true;
}

/* Fan task only */
static void save_state(const fan_state_t &st)
{
    if (st.speed == s_saved.speed && st.last_speed == s_saved.last_speed && st.swing == s_saved.swing) {
        return;
    }
    const StoredState stored = {k_state_version, st.speed, st.last_speed, (uint8_t)st.swing};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_nvs_key_state, &stored, sizeof(stored));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store the fan state: %s", esp_err_to_name(err));
        return;
    }
    s_saved = st;
}

fan_state_t fan_ctrl_get_state()
{
    taskENTER_CRITICAL(&s_lock);
    const fan_state_t st = s_state;
    taskEXIT_CRITICAL(&s_lock);
    return st;
}

static void log_state(const fan_state_t &st, const char *why)
{
    if (st.speed) {
        ESP_LOGI(TAG, "Fan speed %u (%u%%), oscillation %s (%s)", st.speed, fan_speed_to_percent(st.speed),
                 st.swing ? "on" : "off", why);
    } else {
        ESP_LOGI(TAG, "Fan off (next on: speed %u, oscillation %s) (%s)", st.last_speed, st.swing ? "on" : "off",
                 why);
    }
}

static void fan_task(void *)
{
    /* Apply the restored state here rather than in app_main: the dead time may have to elapse first */
    fan_state_t st = fan_ctrl_get_state();
    relays_apply(st);
    status_led_running(st.speed > 0);

    bool save_pending = false;
    TickType_t changed_tick = 0;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (save_pending) {
            const TickType_t elapsed = xTaskGetTickCount() - changed_tick;
            const TickType_t delay = pdMS_TO_TICKS(k_save_delay_ms);
            wait = elapsed >= delay ? 0 : delay - elapsed;
        }
        Msg msg;
        if (xQueueReceive(s_queue, &msg, wait) != pdTRUE) {
            save_state(fan_ctrl_get_state());
            save_pending = false;
            continue;
        }

        st = fan_ctrl_get_state();
        const bool changed = fan_state_apply(&st, &msg.cmd);
        if (changed) {
            taskENTER_CRITICAL(&s_lock);
            s_state = st;
            taskEXIT_CRITICAL(&s_lock);
            relays_apply(st);
            log_state(st, src_name(msg.src));
            status_led_running(st.speed > 0);
            save_pending = true;
            changed_tick = xTaskGetTickCount();
        }
        if (s_listener) {
            s_listener(&st, (fan_src_t)msg.src, changed);
        }
    }
}

static void boot_ok_cb(void *)
{
    s_boot_pending = 0;
}

void fan_ctrl_init()
{
    relays_init();

    /* Power-on reset: RTC memory holds garbage, never a crash loop. Brownout: the supply dipped, maybe because of
     * the relays: counts as a crash when the previous start was younger than 30 s. */
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool crashed = reason != ESP_RST_POWERON && s_boot_pending == k_boot_pending_magic;
    const bool power_cut = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
    s_boot_pending = k_boot_pending_magic;
    const esp_timer_create_args_t timer_args = {
        .callback = boot_ok_cb,
        .name = "fan_boot_ok",
    };
    if (esp_timer_create(&timer_args, &s_boot_ok_timer) == ESP_OK) {
        esp_timer_start_once(s_boot_ok_timer, k_boot_ok_delay_us);
    }

    fan_state_t st = {0, FAN_DEFAULT_SPEED, false};
    const bool loaded = load_state(&st);
    s_saved = st;
    const char *why = loaded ? "restored" : "first start";
    if (crashed && st.speed) {
        ESP_LOGW(TAG, "The previous start crashed within %llu s (reset reason %d): fan stays off",
                 (unsigned long long)(k_boot_ok_delay_us / 1000000), (int)reason);
        st.speed = 0;
        why = "after a crash";
#if CONFIG_FAN_POWER_ON_OFF
    } else if (power_cut && st.speed) {
        st.speed = 0;
        why = "power on: off";
#endif
    }
    (void)power_cut;
    s_state = st;
    log_state(st, why);

    s_queue = xQueueCreate(16, sizeof(Msg));
    if (s_queue == NULL || xTaskCreate(fan_task, "fan", 4096, NULL, 6, NULL) != pdPASS) {
        /* Without the task nothing can switch the relays: they stay open */
        ESP_LOGE(TAG, "Failed to start the fan task");
        if (s_queue) {
            vQueueDelete(s_queue);
            s_queue = NULL;
        }
    }
}

bool fan_ctrl_post(const fan_cmd_t *cmd, fan_src_t src)
{
    if (s_queue == NULL) {
        return false;
    }
    const Msg msg = {*cmd, (uint8_t)src};
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, %s command dropped", src_name(src));
        return false;
    }
    return true;
}

bool fan_ctrl_post_type(fan_cmd_type_t type, uint8_t arg, fan_src_t src)
{
    const fan_cmd_t cmd = {(uint8_t)type, arg, false, false};
    return fan_ctrl_post(&cmd, src);
}

void fan_ctrl_set_listener(fan_listener_t listener)
{
    s_listener = listener;
}
