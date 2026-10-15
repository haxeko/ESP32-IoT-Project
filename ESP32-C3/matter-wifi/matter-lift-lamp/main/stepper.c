#include "stepper.h"

#include <sdkconfig.h>

#if CONFIG_MOTOR_ENABLED

#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

#include "motion.h"

static const char *TAG = "stepper";

#define TICK_HZ           20000 /* motion planner rate; max 10000 steps/s */
#define TIMER_RES_HZ      1000000
#define ENDSTOP_DEBOUNCE  4    /* ticks (200 us) pressed in a row */
#define BACKOFF_MM        3    /* leave the end stop by this much before touching it again slowly */
#define SLOW_TOUCH_DIV    4    /* second touch at homing speed / 4 */

/* Every GPIO used once (menuconfig) */
#define PIN_CLASH(a, b) ((a) >= 0 && (a) == (b))
_Static_assert(!PIN_CLASH(CONFIG_MOTOR_STEP_GPIO, CONFIG_MOTOR_DIR_GPIO) &&
                   !PIN_CLASH(CONFIG_MOTOR_STEP_GPIO, CONFIG_MOTOR_EN_GPIO) &&
                   !PIN_CLASH(CONFIG_MOTOR_STEP_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO) &&
                   !PIN_CLASH(CONFIG_MOTOR_DIR_GPIO, CONFIG_MOTOR_EN_GPIO) &&
                   !PIN_CLASH(CONFIG_MOTOR_DIR_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO) &&
                   !PIN_CLASH(CONFIG_MOTOR_EN_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO),
               "motor GPIOs must all differ");
_Static_assert(!PIN_CLASH(CONFIG_LED_GPIO, CONFIG_MOTOR_STEP_GPIO) && !PIN_CLASH(CONFIG_LED_GPIO, CONFIG_MOTOR_DIR_GPIO) &&
                   !PIN_CLASH(CONFIG_LED_GPIO, CONFIG_MOTOR_EN_GPIO) &&
                   !PIN_CLASH(CONFIG_LED_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO),
               "a motor GPIO is the WS2812 GPIO");
_Static_assert(!PIN_CLASH(CONFIG_APP_BUTTON_GPIO, CONFIG_MOTOR_STEP_GPIO) &&
                   !PIN_CLASH(CONFIG_APP_BUTTON_GPIO, CONFIG_MOTOR_DIR_GPIO) &&
                   !PIN_CLASH(CONFIG_APP_BUTTON_GPIO, CONFIG_MOTOR_EN_GPIO) &&
                   !PIN_CLASH(CONFIG_APP_BUTTON_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO),
               "a motor GPIO is the button GPIO");

#define STEP_BIT BIT(CONFIG_MOTOR_STEP_GPIO)
#define DIR_BIT  BIT(CONFIG_MOTOR_DIR_GPIO)

/* Motor geometry: steps per mm = steps/rev * microsteps * 1000 / lead (um per rev) */
static int32_t mm_to_steps(uint32_t mm)
{
    return (int32_t)((uint64_t)mm * CONFIG_MOTOR_STEPS_PER_REV * CONFIG_MOTOR_MICROSTEPS * 1000 / CONFIG_MOTOR_LEAD_UM);
}

static motion_t s_m; /* owned by the interrupt, see motion.h */
static gptimer_handle_t s_timer;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock; /* start/stop of the timer, driver enable, limits */
static bool s_running;           /* timer interrupt running (s_lock) */
static bool s_driver_on;         /* EN asserted (s_lock) */
static int64_t s_idle_since_us;  /* when the timer was last stopped (s_lock) */
static uint32_t s_vmax_normal, s_accel_normal;
static int32_t s_rezero_tolerance; /* end stop pressed this far above 0: steps were lost (1 mm) */

/* Interrupt state */
static bool s_step_high;
static int8_t s_dir_out;
static uint8_t s_endstop_count;
static volatile bool s_endstop_hit; /* the end stop stopped a downward motion */
static volatile bool s_rezeroed;    /* ... at a position other than 0: steps were lost */

static inline bool IRAM_ATTR endstop_pressed_raw(void)
{
    bool high = (REG_READ(GPIO_IN_REG) >> CONFIG_MOTOR_ENDSTOP_GPIO) & 1;
#if CONFIG_MOTOR_ENDSTOP_NC
    return high; /* normally closed to GND: opens (pull-up reads high) when pressed, or when a wire breaks */
#else
    return !high; /* normally open to GND */
#endif
}

static inline void IRAM_ATTR set_dir_pin(int8_t dir)
{
#if CONFIG_MOTOR_DIR_INVERT
    bool up_level = false;
#else
    bool up_level = true;
#endif
    bool level = dir > 0 ? up_level : !up_level;
    REG_WRITE(level ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, DIR_BIT);
}

/* 20 kHz, in IRAM and cache-safe (CONFIG_GPTIMER_ISR_CACHE_SAFE): steps keep coming during flash writes */
static bool IRAM_ATTR on_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    if (s_step_high) { /* end of the step pulse started on the previous tick (50 us high) */
        REG_WRITE(GPIO_OUT_W1TC_REG, STEP_BIT);
        s_step_high = false;
    }

    if (endstop_pressed_raw()) {
        if (s_endstop_count < ENDSTOP_DEBOUNCE) {
            s_endstop_count++;
        }
    } else {
        s_endstop_count = 0;
    }

    /* Going down (or about to) onto a pressed end stop: this is position 0, never step further down */
    bool going_down = s_m.dir < 0 || (s_m.dir == 0 && s_m.target < s_m.pos);
    if (s_endstop_count >= ENDSTOP_DEBOUNCE && going_down) {
        if (s_m.pos > s_rezero_tolerance && s_m.target >= 0) {
            s_rezeroed = true; /* normal motion (homing targets are below 0) found the end stop early */
        }
        motion_halt(&s_m, 0);
        s_endstop_hit = true;
        return false;
    }

    int step = motion_tick(&s_m);
    if (s_m.dir != 0 && s_m.dir != s_dir_out) { /* only changes at rest, many ticks before the first step */
        set_dir_pin(s_m.dir);
        s_dir_out = s_m.dir;
    }
    if (step) {
        REG_WRITE(GPIO_OUT_W1TS_REG, STEP_BIT);
        s_step_high = true;
    }
    return false;
}

static void driver_enable(bool on) /* s_lock held */
{
#if CONFIG_MOTOR_EN_GPIO >= 0
    if (on != s_driver_on) {
        gpio_set_level(CONFIG_MOTOR_EN_GPIO, on ? 0 : 1); /* EN is active low on TMC2209, A4988, DRV8825 */
        if (on) {
            vTaskDelay(pdMS_TO_TICKS(2)); /* let the driver power its outputs before the first step */
        }
    }
#endif
    s_driver_on = on;
}

/* s_lock held */
static void set_target_locked(int32_t pos)
{
    s_m.target = pos;
    if (!s_running) {
        driver_enable(true);
        ESP_ERROR_CHECK(gptimer_start(s_timer));
        s_running = true;
    }
}

static void set_target(int32_t pos)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_target_locked(pos);
    xSemaphoreGive(s_lock);
}

static void set_limits(uint32_t vmax, uint32_t accel)
{
    s_m.accel = accel; /* single word stores, read by the interrupt */
    s_m.vmax = vmax;
}

int32_t stepper_travel_steps(void)
{
    return mm_to_steps(CONFIG_MOTOR_TRAVEL_MM);
}

esp_err_t stepper_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    uint64_t outputs = BIT64(CONFIG_MOTOR_STEP_GPIO) | BIT64(CONFIG_MOTOR_DIR_GPIO);
#if CONFIG_MOTOR_EN_GPIO >= 0
    outputs |= BIT64(CONFIG_MOTOR_EN_GPIO);
    gpio_set_level(CONFIG_MOTOR_EN_GPIO, 1); /* driver off until the first motion */
#endif
    const gpio_config_t out = {
        .pin_bit_mask = outputs,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "outputs");
    gpio_set_level(CONFIG_MOTOR_STEP_GPIO, 0);
#if CONFIG_MOTOR_EN_GPIO >= 0
    gpio_set_level(CONFIG_MOTOR_EN_GPIO, 1);
#endif
    const gpio_config_t in = {
        .pin_bit_mask = BIT64(CONFIG_MOTOR_ENDSTOP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "end stop");

    uint32_t steps_per_s = (uint32_t)mm_to_steps(CONFIG_MOTOR_SPEED_MM_S);
    if (steps_per_s > TICK_HZ / 2) {
        ESP_LOGW(TAG, "Speed %d mm/s needs %lu steps/s, limited to %d", CONFIG_MOTOR_SPEED_MM_S,
                 (unsigned long)steps_per_s, TICK_HZ / 2);
    }
    s_vmax_normal = motion_speed_q32(steps_per_s, TICK_HZ);
    s_accel_normal = motion_accel_q32((uint32_t)mm_to_steps(CONFIG_MOTOR_ACCEL_MM_S2), TICK_HZ);
    motion_init(&s_m, s_vmax_normal, s_accel_normal);
    s_dir_out = 0;
    s_rezero_tolerance = mm_to_steps(1);

    const gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RES_HZ,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&timer_config, &s_timer), TAG, "timer");
    const gptimer_event_callbacks_t cbs = {
        .on_alarm = on_tick,
    };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_timer, &cbs, NULL), TAG, "timer callback");
    ESP_RETURN_ON_ERROR(gptimer_enable(s_timer), TAG, "timer enable");
    const gptimer_alarm_config_t alarm = {
        .alarm_count = TIMER_RES_HZ / TICK_HZ,
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = true,
        },
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_timer, &alarm), TAG, "timer alarm");

#if CONFIG_MOTOR_ENDSTOP_NC
    const char *contact = "NC";
#else
    const char *contact = "NO";
#endif
    ESP_LOGI(TAG, "STEP GPIO%d DIR GPIO%d EN GPIO%d end stop GPIO%d (%s), %ld steps/mm, travel %d mm = %ld steps",
             CONFIG_MOTOR_STEP_GPIO, CONFIG_MOTOR_DIR_GPIO, CONFIG_MOTOR_EN_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO, contact,
             (long)mm_to_steps(1), CONFIG_MOTOR_TRAVEL_MM, (long)stepper_travel_steps());
    return ESP_OK;
}

/* Wait until the motion is at rest, or (stop_on_endstop) the end stop stopped it. false on timeout. */
static bool wait_motion(uint32_t timeout_ms, bool stop_on_endstop)
{
    for (uint32_t t = 0; t < timeout_ms; t += 10) {
        if (stop_on_endstop && s_endstop_hit) {
            return true;
        }
        if (motion_idle(&s_m)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

/* Time to move a distance at a speed (+ acceleration and margin) */
static uint32_t move_time_ms(int32_t steps, uint32_t steps_per_s)
{
    if (steps < 0) {
        steps = -steps;
    }
    return (uint32_t)((uint64_t)steps * 1000 / (steps_per_s ? steps_per_s : 1)) + 3000;
}

esp_err_t stepper_home(void)
{
    uint32_t fast = (uint32_t)mm_to_steps(CONFIG_MOTOR_HOMING_SPEED_MM_S);
    uint32_t slow = fast / SLOW_TOUCH_DIV ? fast / SLOW_TOUCH_DIV : 1;
    int32_t backoff = mm_to_steps(BACKOFF_MM);
    int32_t search = stepper_travel_steps() + stepper_travel_steps() / 4;
    esp_err_t err = ESP_OK;

    set_limits(motion_speed_q32(fast, TICK_HZ), s_accel_normal);

    /* 1. Pressed already (the lamp is closed): move up off it, it must release */
    if (s_endstop_count >= ENDSTOP_DEBOUNCE || endstop_pressed_raw()) {
        ESP_LOGI(TAG, "Homing: end stop pressed, moving up %d mm", BACKOFF_MM);
        set_target(s_m.pos + backoff);
        wait_motion(move_time_ms(backoff, fast), false);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (endstop_pressed_raw()) {
            ESP_LOGE(TAG, "Homing: end stop still pressed after moving up (check its wiring, the motor and the "
                          "driver power)");
            err = ESP_ERR_INVALID_STATE;
            goto done;
        }
    }

    /* 2. Down until the end stop */
    ESP_LOGI(TAG, "Homing: moving down to the end stop");
    s_endstop_hit = false;
    set_target(s_m.pos - search);
    if (!wait_motion(move_time_ms(search, fast), true) || !s_endstop_hit) {
        ESP_LOGE(TAG, "Homing: end stop not reached within %d mm", CONFIG_MOTOR_TRAVEL_MM * 5 / 4);
        err = ESP_ERR_NOT_FOUND;
        goto done;
    }

    /* 3. Up a little, then touch it again slowly for a repeatable zero */
    set_target(backoff);
    wait_motion(move_time_ms(backoff, fast), false);
    set_limits(motion_speed_q32(slow, TICK_HZ), s_accel_normal);
    s_endstop_hit = false;
    set_target(-2 * backoff);
    if (!wait_motion(move_time_ms(2 * backoff, slow), true) || !s_endstop_hit) {
        ESP_LOGE(TAG, "Homing: end stop not found again");
        err = ESP_ERR_NOT_FOUND;
        goto done;
    }
    ESP_LOGI(TAG, "Homing done");

done:
    /* Stay where we are (position 0 on success), normal limits */
    set_target(err == ESP_OK ? 0 : s_m.pos);
    set_limits(s_vmax_normal, s_accel_normal);
    s_rezeroed = false;
    return err;
}

void stepper_move_to(int32_t pos)
{
    int32_t max = stepper_travel_steps();
    set_target(pos < 0 ? 0 : pos > max ? max : pos);
}

int32_t stepper_position(void)
{
    return s_m.pos;
}

bool stepper_busy(void)
{
    return !motion_idle(&s_m);
}

bool stepper_poll(void)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* The target only changes under s_lock: at rest now means at rest until the next set_target() */
    if (s_running && motion_idle(&s_m)) {
        ESP_ERROR_CHECK(gptimer_stop(s_timer));
        s_running = false;
        s_idle_since_us = now;
        if (s_step_high) { /* the pulse of the very last step */
            gpio_set_level(CONFIG_MOTOR_STEP_GPIO, 0);
            s_step_high = false;
        }
    }
#if CONFIG_MOTOR_IDLE_OFF_MS > 0
    if (!s_running && s_driver_on && now - s_idle_since_us >= (int64_t)CONFIG_MOTOR_IDLE_OFF_MS * 1000) {
        driver_enable(false);
    }
#endif
    xSemaphoreGive(s_lock);

    bool rezeroed = s_rezeroed;
    s_rezeroed = false;
    return rezeroed;
}

void stepper_disable(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_running) {
        gptimer_stop(s_timer);
        s_running = false;
    }
    s_m.target = s_m.pos;
    motion_halt(&s_m, s_m.pos);
    driver_enable(false);
    xSemaphoreGive(s_lock);
}

#endif /* CONFIG_MOTOR_ENABLED */
