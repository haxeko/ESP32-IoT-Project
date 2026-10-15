#include <fan_ctrl.h>

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <fan_store.h>
#include <relays.h>
#include <status_led.h>

static const char *TAG = "fan_ctrl";

static_assert(CONFIG_FAN_NATURAL_MAX_S >= CONFIG_FAN_NATURAL_MIN_S,
              "menuconfig -> Fan Behavior: the natural wind maximum hold time is shorter than the minimum");

/* ---- Fan state ---- */

namespace {

struct Msg {
    fan_cmd_t cmd;
    fan_src_t src;
};

/* Store the state once it has not changed for this long (a few quick presses on the remote = one flash write).
 * esp_restart() (OTA, factory reset) stores a younger change at once, see fan_ctrl_shutdown(). */
constexpr uint32_t k_save_delay_ms = 3000;

/* A panic, watchdog or brownout reset of a start younger than this counts as a crash loop: the next start keeps
 * the fan off */
constexpr uint64_t k_boot_ok_delay_us = 30 * 1000 * 1000ULL;
constexpr uint32_t k_boot_pending_magic = 0x46414E31; /* "FAN1" */

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
fan_state_t s_state;   /* written by the fan task (and fan_ctrl_init), guarded by s_lock */
uint8_t s_drive_speed; /* speed relay the fan task drives (natural wind varies it), guarded by s_lock */
fan_state_t s_saved;   /* fan task only: last state written to NVS */
QueueHandle_t s_queue;
fan_listener_t s_listener;
esp_timer_handle_t s_boot_ok_timer;

/* Survives software resets, panics and watchdogs (not power-on): set while a start is younger than
 * k_boot_ok_delay_us, cleared by a deliberate esp_restart() */
RTC_NOINIT_ATTR uint32_t s_boot_pending;

} // namespace

static const char *src_name(fan_src_t src)
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

/* Fan task only (and fan_ctrl_init before the task starts) */
static void save_state(const fan_state_t &st)
{
    if (st.speed == s_saved.speed && st.last_speed == s_saved.last_speed && st.swing == s_saved.swing &&
        st.natural == s_saved.natural) {
        return;
    }
    if (fan_store_write(&st) == ESP_OK) {
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
    ESP_LOGD(TAG, "Natural wind: speed %u for %lu s", s_natural.speed, (unsigned long)(hold_ms / 1000));
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
    relays_apply(speed, fan_state_swing_running(&st));
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
        notify(st, msg.src, changed);
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
    const fan_state_t st = fan_ctrl_get_state();
    fan_store_write(&st);
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
    if (esp_timer_create(&timer_args, &s_boot_ok_timer) != ESP_OK ||
        esp_timer_start_once(s_boot_ok_timer, k_boot_ok_delay_us) != ESP_OK) {
        /* The start cannot be timed: never count a later reset as a crash loop */
        s_boot_pending = 0;
        ESP_LOGW(TAG, "Failed to start the boot timer");
    }

    fan_state_t st = {0, FAN_DEFAULT_SPEED, false, false};
    const bool loaded = fan_store_load(&st);
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
    const Msg msg = {*cmd, src};
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
