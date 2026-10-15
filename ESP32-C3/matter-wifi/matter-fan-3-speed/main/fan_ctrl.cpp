#include <fan_ctrl.h>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_random.h>
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
static_assert(CONFIG_FAN_NATURAL_MAX_S >= CONFIG_FAN_NATURAL_MIN_S,
              "menuconfig -> Fan Behavior: the natural wind maximum hold time is shorter than the minimum");
static_assert(pins_off_console(), "GPIO20/21 carry the UART0 log console: check menuconfig -> Fan Relays / IR "
                                  "Receiver / Buttons / Status LED (or move the console to USB-Serial-JTAG)");

/* ---- Fan state ---- */

namespace {

struct Msg {
    fan_cmd_t cmd;
    uint8_t src; /* fan_src_t */
};

/* NVS record of the state. Same namespace as the learned IR codes. Version 1 had no natural byte. */
struct StoredState {
    uint8_t version;
    uint8_t speed;
    uint8_t last_speed;
    uint8_t swing;
    uint8_t natural;
};
constexpr uint8_t k_state_version = 2;
constexpr size_t k_state_v1_size = 4;
constexpr const char *k_nvs_namespace = "fan";
constexpr const char *k_nvs_key_state = "state";

const gpio_num_t k_speed_gpio[FAN_SPEED_COUNT] = {
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED1_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED2_GPIO,
    (gpio_num_t)CONFIG_FAN_RELAY_SPEED3_GPIO,
};
constexpr gpio_num_t k_swing_gpio = (gpio_num_t)CONFIG_FAN_RELAY_SWING_GPIO;
constexpr int64_t k_dead_time_us = CONFIG_FAN_RELAY_DEAD_TIME_MS * 1000LL;

/* Store the state once it has not changed for this long (a few quick presses on the remote = one flash write).
 * esp_restart() (OTA, factory reset) stores a younger change at once, see fan_ctrl_shutdown(). */
constexpr uint32_t k_save_delay_ms = 3000;

/* A panic, watchdog or brownout reset of a start younger than this counts as a crash loop: the next start keeps
 * the fan off */
constexpr uint64_t k_boot_ok_delay_us = 30 * 1000 * 1000ULL;
constexpr uint32_t k_boot_pending_magic = 0x46414E31; /* "FAN1" */

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
fan_state_t s_state;          /* written by the fan task (and fan_ctrl_init), guarded by s_lock */
uint8_t s_drive_speed;        /* speed relay the fan task drives (natural wind varies it), guarded by s_lock */
fan_state_t s_saved;          /* fan task only: last state written to NVS */
uint8_t s_phys_speed;         /* fan task only: speed relay that is closed (0 = none) */
bool s_phys_swing;            /* fan task only */
int64_t s_speed_released_us;  /* fan task only: when a speed relay was last released */
QueueHandle_t s_queue;
fan_listener_t s_listener;
esp_timer_handle_t s_boot_ok_timer;

/* Survives software resets, panics and watchdogs (not power-on): set while a start is younger than
 * k_boot_ok_delay_us, cleared by a deliberate esp_restart() */
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
    case FAN_SRC_NATURAL:
        return "natural wind";
    default:
        return "?";
    }
}

static int relay_level(bool closed)
{
    return closed ? CONFIG_FAN_RELAY_ACTIVE_LEVEL : !CONFIG_FAN_RELAY_ACTIVE_LEVEL;
}

/* Fan task only: wait until FAN_RELAY_DEAD_TIME_MS have passed since a speed relay was released. vTaskDelay(n) can
 * return after only n - 1 full ticks, so check the microsecond clock again. */
static void wait_dead_time()
{
    constexpr int64_t tick_us = portTICK_PERIOD_MS * 1000;
    for (;;) {
        const int64_t left_us = s_speed_released_us + k_dead_time_us - esp_timer_get_time();
        if (left_us <= 0) {
            return;
        }
        vTaskDelay((TickType_t)((left_us + tick_us - 1) / tick_us));
    }
}

/* Fan task only. Break before make: one speed relay at a time, and FAN_RELAY_DEAD_TIME_MS between releasing one
 * and closing another (also across a quick off -> on), so two motor taps are never on the mains together.
 * speed: the speed relay to close (natural wind: the speed of the moment, not st.speed). */
static void relays_apply(const fan_state_t &st, uint8_t speed)
{
    const bool swing = fan_state_swing_running(&st);
    const uint8_t old_speed = s_phys_speed;
    const bool old_swing = s_phys_swing;

    /* The oscillation relay opens before the fan is switched off and closes after it is switched on. A speed change
     * does not interrupt it: the fan coasts for the dead time only. */
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
            wait_dead_time();
            gpio_set_level(k_speed_gpio[speed - 1], relay_level(true));
            s_phys_speed = speed;
        }
    }
    if (swing && !s_phys_swing) {
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
    StoredState stored = {};
    size_t len = sizeof(stored);
    const esp_err_t err = nvs_get_blob(handle, k_nvs_key_state, &stored, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }
    const bool v1 = stored.version == 1 && len == k_state_v1_size; /* before natural wind: off */
    if (!v1 && (stored.version != k_state_version || len != sizeof(stored))) {
        return false;
    }
    st->speed = stored.speed;
    st->last_speed = stored.last_speed;
    st->swing = stored.swing != 0;
    st->natural = !v1 && stored.natural != 0;
    fan_state_sanitize(st);
    return true;
}

static esp_err_t write_record(const fan_state_t &st)
{
    const StoredState stored = {k_state_version, st.speed, st.last_speed, (uint8_t)st.swing, (uint8_t)st.natural};
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
    }
    return err;
}

/* Fan task only (and fan_ctrl_init before the task starts) */
static void save_state(const fan_state_t &st)
{
    if (st.speed == s_saved.speed && st.last_speed == s_saved.last_speed && st.swing == s_saved.swing &&
        st.natural == s_saved.natural) {
        return;
    }
    if (write_record(st) == ESP_OK) {
        s_saved = st;
    }
}

fan_state_t fan_ctrl_get_state()
{
    taskENTER_CRITICAL(&s_lock);
    const fan_state_t st = s_state;
    taskEXIT_CRITICAL(&s_lock);
    return st;
}

fan_snapshot_t fan_ctrl_get_snapshot()
{
    taskENTER_CRITICAL(&s_lock);
    const fan_snapshot_t snap = {s_state, s_drive_speed};
    taskEXIT_CRITICAL(&s_lock);
    return snap;
}

static void log_state(const fan_state_t &st, const char *why)
{
    const char *const natural = st.natural ? ", natural wind" : "";
    if (st.speed) {
        ESP_LOGI(TAG, "Fan speed %u (%u%%)%s, oscillation %s (%s)", st.speed, fan_speed_to_percent(st.speed), natural,
                 st.swing ? "on" : "off", why);
    } else {
        ESP_LOGI(TAG, "Fan off (next on: speed %u%s, oscillation %s) (%s)", st.last_speed, natural,
                 st.swing ? "on" : "off", why);
    }
}

/* ---- Natural wind (fan task only) ---- */

namespace {

struct NaturalWind {
    bool active;       /* the pattern drives the speed relays */
    uint8_t speed;     /* speed of the moment */
    TickType_t since;  /* when it was chosen */
    TickType_t hold;   /* how long it is held */
    uint32_t rng;
};
NaturalWind s_natural;

} // namespace

/* Next speed of the pattern, around the speed setting */
static void natural_step(uint8_t setting)
{
    uint32_t hold_ms;
    const uint8_t current = s_natural.active ? s_natural.speed : FAN_NATURAL_START;
    s_natural.speed = fan_natural_next(&s_natural.rng, current, setting, CONFIG_FAN_NATURAL_MIN_S,
                                       CONFIG_FAN_NATURAL_MAX_S, &hold_ms);
    s_natural.active = true;
    s_natural.since = xTaskGetTickCount();
    s_natural.hold = pdMS_TO_TICKS(hold_ms);
    ESP_LOGI(TAG, "Natural wind: speed %u for %lu s", s_natural.speed, (unsigned long)(hold_ms / 1000));
}

static TickType_t ticks_left(TickType_t since, TickType_t delay)
{
    const TickType_t elapsed = xTaskGetTickCount() - since;
    return elapsed >= delay ? 0 : delay - elapsed;
}

/* The speed relay for this state; starts, adjusts or stops the natural wind pattern */
static uint8_t drive_speed(const fan_state_t &st)
{
    if (!fan_state_natural_running(&st)) {
        s_natural.active = false;
        return st.speed;
    }
    /* Natural wind starts, or the speed setting changed and the speed of the moment is out of the new range */
    uint8_t low, high;
    fan_natural_range(st.speed, &low, &high);
    if (!s_natural.active || s_natural.speed < low || s_natural.speed > high) {
        natural_step(st.speed);
    }
    return s_natural.speed;
}

/* ---- Fan task ---- */

/* Switch the relays and publish the speed they drive */
static void drive(const fan_state_t &st)
{
    const uint8_t speed = drive_speed(st);
    relays_apply(st, speed);
    taskENTER_CRITICAL(&s_lock);
    s_drive_speed = speed;
    taskEXIT_CRITICAL(&s_lock);
}

static void notify(const fan_state_t &st, fan_src_t src, bool changed)
{
    if (s_listener) {
        s_listener(&st, src, changed);
    }
}

static void fan_task(void *)
{
    s_natural.rng = esp_random() | 1;
    /* Apply the restored state here rather than in app_main: the dead time may have to elapse first */
    fan_state_t st = fan_ctrl_get_state();
    drive(st);
    status_led_running(st.speed > 0);

    bool save_pending = false;
    TickType_t changed_tick = 0;
    const TickType_t save_delay = pdMS_TO_TICKS(k_save_delay_ms);
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (save_pending) {
            wait = ticks_left(changed_tick, save_delay);
        }
        if (s_natural.active) {
            const TickType_t natural_wait = ticks_left(s_natural.since, s_natural.hold);
            wait = natural_wait < wait ? natural_wait : wait;
        }
        Msg msg;
        if (xQueueReceive(s_queue, &msg, wait) != pdTRUE) {
            /* Timeout: store the state, and/or the next step of the natural wind */
            if (save_pending && ticks_left(changed_tick, save_delay) == 0) {
                save_state(st);
                save_pending = false;
            }
            if (s_natural.active && ticks_left(s_natural.since, s_natural.hold) == 0) {
                natural_step(st.speed);
                drive(st);
                notify(st, FAN_SRC_NATURAL, true);
            }
            continue;
        }

        const bool changed = fan_state_apply(&st, &msg.cmd);
        if (changed) {
            taskENTER_CRITICAL(&s_lock);
            s_state = st;
            taskEXIT_CRITICAL(&s_lock);
            drive(st);
            log_state(st, src_name(msg.src));
            status_led_running(st.speed > 0);
            save_pending = true;
            changed_tick = xTaskGetTickCount();
        }
        /* Also when unchanged: Matter writes the real state back over a setting the fan did not take */
        notify(st, (fan_src_t)msg.src, changed);
    }
}

static void boot_ok_cb(void *)
{
    s_boot_pending = 0;
}

/* esp_restart() (OTA update, factory reset, ...), from the restarting task: a deliberate restart, not a crash.
 * Also stores a change younger than the save delay, so the restart does not revert it. Not called on panics,
 * watchdogs or brownouts. */
static void fan_ctrl_shutdown()
{
    s_boot_pending = 0;
    write_record(fan_ctrl_get_state());
}

static bool reset_may_be_crash(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT: /* the supply dipped, maybe because of the relays */
        return true;
    default:
        return false;
    }
}

void fan_ctrl_init()
{
    relays_init();

    /* Crash loop: a panic, watchdog or brownout reset while the previous start was younger than 30 s (power-on resets
     * leave garbage in RTC memory and are never a crash) */
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool crashed = reset_may_be_crash(reason) && s_boot_pending == k_boot_pending_magic;
    const bool power_cut = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
    s_boot_pending = k_boot_pending_magic;
    if (esp_register_shutdown_handler(fan_ctrl_shutdown) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register the shutdown handler");
    }
    const esp_timer_create_args_t timer_args = {
        .callback = boot_ok_cb,
        .name = "fan_boot_ok",
    };
    if (esp_timer_create(&timer_args, &s_boot_ok_timer) == ESP_OK) {
        esp_timer_start_once(s_boot_ok_timer, k_boot_ok_delay_us);
    }

    fan_state_t st = {0, FAN_DEFAULT_SPEED, false, false};
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
    /* Kept off at this start: store it, so a later software restart does not switch the fan back on */
    save_state(st);

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
