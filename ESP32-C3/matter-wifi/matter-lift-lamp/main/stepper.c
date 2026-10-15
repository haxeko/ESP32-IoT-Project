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

#define TICK_HZ          20000 /* motion planner rate; max 10000 steps/s */
#define TIMER_RES_HZ     1000000
#define ENDSTOP_DEBOUNCE 20 /* ticks (1 ms) pressed in a row: contact chatter and noise are ignored */
#define BACKOFF_MM       3  /* homing: leave the end stop by this much before touching it again slowly */
#define SLOW_TOUCH_DIV   4  /* homing: second touch at homing speed / 4 */
#define ZERO_WINDOW_MM   3  /* the end stop is only believed within this distance of 0; elsewhere it is a fault */
#define CLOSE_SLOW_MM    10 /* closing: the last 10 mm at the homing speed, onto the end stop */
#define CLOSE_SEEK_MM    5  /* closing: look for the end stop down to 5 mm below 0 */
#define RELEASE_MM       10 /* closing did not find the end stop (something in the way): back up 10 mm, let go */

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
static SemaphoreHandle_t s_lock; /* everything below marked (s_lock): task side only */
static bool s_running;           /* timer interrupt running (s_lock) */
static bool s_driver_on;         /* EN asserted (s_lock) */
static int64_t s_idle_since_us;  /* when the timer was last stopped (s_lock) */
static uint32_t s_vmax_normal, s_accel_normal, s_vmax_homing; /* motion.h units */
static uint32_t s_accel_sps2;     /* steps/s^2 */
static int32_t s_rezero_tolerance; /* re-zero this far (1 mm) from 0: steps were lost, reported */
static int32_t s_window;           /* ZERO_WINDOW_MM in steps */
static bool s_homed;               /* the position is known (s_lock) */
static bool s_ready;               /* homed, and no fault since: stepper_move_to() works (s_lock) */
static int32_t s_close_peak;       /* highest position since the closing began: the sleeve was no higher (s_lock) */
/* After a closing that missed the end stop: the sleeve is at most this high (it only went down, then backed up),
 * so homing searches only this far down (plus ZERO_WINDOW_MM). 0: position not lost, search 125 % of the travel. */
static int32_t s_lost_top; /* (s_lock) */

/* Closing (target 0) runs in phases, driven by stepper_poll() (s_lock) */
static enum {
    CLOSE_IDLE,
    CLOSE_APPROACH, /* normal speed down to CLOSE_SLOW_MM */
    CLOSE_TOUCH,    /* homing speed down onto the end stop, at most CLOSE_SEEK_MM below 0 */
    CLOSE_RELEASE,  /* the end stop was not found: back up RELEASE_MM, then let go */
} s_close;

/* Interrupt state */
static bool s_step_high;
static int8_t s_dir_out;
static uint8_t s_endstop_count;
static volatile bool s_homing;      /* homing runs: any end stop press is position 0 (position unknown) */
static volatile bool s_endstop_hit; /* the end stop stopped a downward motion at 0 */
static volatile bool s_rezeroed;    /* ... more than 1 mm from 0: steps were lost */
static volatile bool s_fault;       /* the end stop read pressed where it cannot be: motion halted */

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

/* The end stop reads pressed where the lamp cannot be closed (broken NC wire, noise, a jam): stop where we are,
 * keep the position (do not trust the end stop), the task re-homes */
static inline void IRAM_ATTR fault_halt(void)
{
    motion_halt(&s_m, s_m.pos);
    s_fault = true;
}

/* 20 kHz, in IRAM and cache-safe (CONFIG_GPTIMER_ISR_CACHE_SAFE): steps keep coming during flash writes */
static bool IRAM_ATTR on_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    if (s_step_high) { /* end of the step pulse started on the previous tick (50 us high) */
        REG_WRITE(GPIO_OUT_W1TC_REG, STEP_BIT);
        s_step_high = false;
    }
    if (s_fault) {
        return false; /* halted until stepper_home() */
    }

    if (endstop_pressed_raw()) {
        if (s_endstop_count < ENDSTOP_DEBOUNCE) {
            s_endstop_count++;
        }
    } else {
        s_endstop_count = 0;
    }

    if (s_endstop_count >= ENDSTOP_DEBOUNCE) {
        bool going_down = s_m.dir < 0 || (s_m.dir == 0 && s_m.target < s_m.pos);
        int32_t pos = s_m.pos;
        if (going_down) {
            if (s_homing || pos <= s_window) {
                /* Closed: this is position 0, never step further down */
                if (!s_homing && (pos > s_rezero_tolerance || pos < -s_rezero_tolerance)) {
                    s_rezeroed = true;
                }
                motion_halt(&s_m, 0);
                s_endstop_hit = true;
            } else {
                fault_halt(); /* pressed far above 0 */
            }
            return false;
        }
        if (s_m.dir > 0 && pos > s_window && !s_homing) {
            fault_halt(); /* still pressed well after leaving 0: it does not release */
            return false;
        }
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
            vTaskDelay(1); /* let the driver power its outputs before the first step (at least one tick) */
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

/* s_lock held: timer stopped (ends the last STEP pulse) */
static void stop_timer_locked(int64_t now)
{
    if (s_running) {
        ESP_ERROR_CHECK(gptimer_stop(s_timer));
        s_running = false;
        s_idle_since_us = now;
    }
    if (s_step_high) {
        gpio_set_level(CONFIG_MOTOR_STEP_GPIO, 0);
        s_step_high = false;
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

/* Steps per second the planner really runs at for a requested rate */
static uint32_t real_sps(uint32_t sps)
{
    return sps > TICK_HZ / 2 ? TICK_HZ / 2 : sps ? sps : 1;
}

/* Where the motion would stop if it braked now (2 steps margin for the rounding of the planner); s_lock held */
static int32_t stop_point_locked(void)
{
    uint32_t vel = s_m.vel; /* single words, changed by the interrupt: a tick apart at worst */
    int8_t dir = s_m.dir;
    int32_t pos = s_m.pos;
    uint64_t v2 = ((uint64_t)vel * vel) >> 32;
    int32_t brake = (int32_t)(v2 / (2 * (uint64_t)s_m.accel)) + 2;
    return dir > 0 ? pos + brake : dir < 0 ? pos - brake : pos;
}

/* Time for a move: distance at the (clamped) speed, plus speeding up and braking, plus 2 s */
static uint32_t move_time_ms(int32_t steps, uint32_t sps)
{
    uint64_t v = real_sps(sps);
    uint64_t d = steps < 0 ? (uint64_t)(-(int64_t)steps) : (uint64_t)steps;
    return (uint32_t)(d * 1000 / v + 2 * v * 1000 / s_accel_sps2 + 2000);
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

    uint32_t speed = (uint32_t)mm_to_steps(CONFIG_MOTOR_SPEED_MM_S);
    uint32_t homing = (uint32_t)mm_to_steps(CONFIG_MOTOR_HOMING_SPEED_MM_S);
    if (speed > TICK_HZ / 2 || homing > TICK_HZ / 2) {
        ESP_LOGW(TAG, "Speed %lu steps/s, homing %lu steps/s: limited to %d steps/s", (unsigned long)speed,
                 (unsigned long)homing, TICK_HZ / 2);
    }
    s_vmax_normal = motion_speed_q32(speed, TICK_HZ);
    s_vmax_homing = motion_speed_q32(homing, TICK_HZ);
    s_accel_sps2 = (uint32_t)mm_to_steps(CONFIG_MOTOR_ACCEL_MM_S2);
    if (s_accel_sps2 == 0) {
        s_accel_sps2 = 1;
    }
    s_accel_normal = motion_accel_q32(s_accel_sps2, TICK_HZ);
    motion_init(&s_m, s_vmax_normal, s_accel_normal);
    s_dir_out = 0;
    s_rezero_tolerance = mm_to_steps(1);
    s_window = mm_to_steps(ZERO_WINDOW_MM);

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
    ESP_LOGI(TAG, "STEP GPIO%d DIR GPIO%d EN GPIO%d end stop GPIO%d (%s, now %s), %ld steps/mm, travel %d mm = %ld "
                  "steps",
             CONFIG_MOTOR_STEP_GPIO, CONFIG_MOTOR_DIR_GPIO, CONFIG_MOTOR_EN_GPIO, CONFIG_MOTOR_ENDSTOP_GPIO, contact,
             endstop_pressed_raw() ? "pressed" : "released", (long)mm_to_steps(1), CONFIG_MOTOR_TRAVEL_MM,
             (long)stepper_travel_steps());
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

/* The end stop, read now and again 20 ms later (its debounce only runs in the interrupt) */
static bool endstop_pressed_settled(void)
{
    vTaskDelay(pdMS_TO_TICKS(20));
    return endstop_pressed_raw();
}

esp_err_t stepper_home(void)
{
    uint32_t fast = real_sps((uint32_t)mm_to_steps(CONFIG_MOTOR_HOMING_SPEED_MM_S));
    uint32_t slow = real_sps(fast / SLOW_TOUCH_DIV);
    int32_t backoff = mm_to_steps(BACKOFF_MM);
    int32_t travel = stepper_travel_steps();
    int32_t release = mm_to_steps(RELEASE_MM);
    esp_err_t err = ESP_OK;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* After a closing that missed the end stop the sleeve cannot be higher than s_lost_top: do not push on whatever
     * stopped it (it may still be there) for longer than needed */
    int32_t lost_top = s_lost_top;
    int32_t search = lost_top > 0 ? lost_top + s_window : travel + travel / 4;
    s_ready = false;
    s_close = CLOSE_IDLE;
    s_m.target = s_m.pos; /* stopped (the interrupt is halted on a fault) */
    s_fault = false;
    s_homing = true;
    xSemaphoreGive(s_lock);
    set_limits(motion_speed_q32(fast, TICK_HZ), s_accel_normal);

    /* 1. Pressed already (the lamp is closed): move up off it, it must release */
    if (endstop_pressed_settled()) {
        if (s_homed && s_m.pos > s_window && s_m.pos + backoff > travel) {
            /* Known to be near the top: it cannot be pressed there, and moving up would leave the travel */
            ESP_LOGE(TAG, "Homing: end stop reads pressed near the top (broken wire?)");
            err = ESP_ERR_INVALID_STATE;
            goto done;
        }
        ESP_LOGI(TAG, "Homing: end stop pressed, moving up %d mm", BACKOFF_MM);
        set_target(s_m.pos + backoff);
        wait_motion(move_time_ms(backoff, fast), false);
        if (endstop_pressed_settled()) {
            ESP_LOGE(TAG, "Homing: end stop still pressed after moving up (check its wiring, the motor and the "
                          "driver power)");
            err = ESP_ERR_INVALID_STATE;
            goto done;
        }
    }

    /* 2. Down until the end stop */
    ESP_LOGI(TAG, "Homing: moving down to the end stop (at most %ld mm)", (long)(search / mm_to_steps(1)));
    s_endstop_hit = false;
    set_target(s_m.pos - search);
    if (!wait_motion(move_time_ms(search, fast), true) || !s_endstop_hit) {
        /* Something in the way (or no motor, or it turns the wrong way): back up as a failed closing does, rather
         * than keep pressing on it */
        ESP_LOGE(TAG, "Homing: end stop not reached within %ld mm, backing up %d mm", (long)(search / mm_to_steps(1)),
                 RELEASE_MM);
        set_target(s_m.pos + release);
        wait_motion(move_time_ms(release, fast), false);
        err = ESP_ERR_NOT_FOUND;
        goto done;
    }

    /* 3. Up a little: it must release (else the motor cannot lift the sleeve), then touch it again slowly for a
     * repeatable zero */
    set_target(backoff);
    wait_motion(move_time_ms(backoff, fast), false);
    if (endstop_pressed_settled()) {
        ESP_LOGE(TAG, "Homing: end stop does not release when moving up (the motor cannot lift the sleeve?)");
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Stay where we are (position 0 on success), normal limits */
    set_target_locked(err == ESP_OK ? 0 : s_m.pos);
    set_limits(s_vmax_normal, s_accel_normal);
    s_homing = false;
    s_rezeroed = false;
    s_fault = false;
    s_homed = s_ready = err == ESP_OK;
    if (err == ESP_OK) {
        s_lost_top = 0;
    }
    xSemaphoreGive(s_lock);
    return err;
}

/* s_lock held */
static void start_touch_locked(void)
{
    s_close = CLOSE_TOUCH;
    set_limits(s_vmax_homing, s_accel_normal);
    s_endstop_hit = false;
    set_target_locked(-mm_to_steps(CLOSE_SEEK_MM));
}

bool stepper_move_to(int32_t pos)
{
    int32_t max = stepper_travel_steps();
    pos = pos < 0 ? 0 : pos > max ? max : pos;
    bool accepted = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_ready) {
        /* not homed, stopped by a fault, or a closing missed the end stop: only stepper_home() moves the motor */
        accepted = false;
    } else if (pos > 0) {
        if (s_close == CLOSE_TOUCH) {
            /* The closing is checking the position on the end stop (at most CLOSE_SLOW_MM + CLOSE_SEEK_MM at the
             * homing speed): let it finish. Moving up now would trust a count that may have lost steps on
             * something in the way, and drive the sleeve past the top by as much. */
            accepted = false;
        } else {
            s_close = CLOSE_IDLE;
            set_limits(s_vmax_normal, s_accel_normal);
            set_target_locked(pos);
        }
    } else if (s_close == CLOSE_IDLE) {
        /* Close: the last CLOSE_SLOW_MM slower, and always onto the end stop (checks the position). Already heading
         * down and stopping inside the slow zone anyway: slow down to the homing speed, never turn back up. */
        int32_t slow_zone = mm_to_steps(CLOSE_SLOW_MM);
        int32_t stop = stop_point_locked();
        s_close_peak = stop > s_m.pos ? stop : s_m.pos;
        if (stop > slow_zone) {
            s_close = CLOSE_APPROACH;
            set_limits(s_vmax_normal, s_accel_normal);
            set_target_locked(slow_zone);
        } else {
            start_touch_locked();
        }
    }
    xSemaphoreGive(s_lock);
    return accepted;
}

int32_t stepper_position(void)
{
    return s_m.pos;
}

bool stepper_busy(void)
{
    return !motion_idle(&s_m) || s_close != CLOSE_IDLE;
}

bool stepper_ready(void)
{
    return s_ready;
}

stepper_event_t stepper_poll(void)
{
    stepper_event_t ev = STEPPER_EVENT_NONE;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_fault && !s_homing) {
        s_close = CLOSE_IDLE;
        s_ready = false;
        s_m.target = s_m.pos; /* at rest where it stopped; the timer stops below */
        s_fault = false;
        ev = STEPPER_EVENT_FAULT;
    }

    switch (s_close) {
    case CLOSE_APPROACH:
        /* Slow down for the last CLOSE_SLOW_MM as soon as the motion brakes for it (within 1 mm), not from a stop */
        if (motion_idle(&s_m) ||
            (s_m.dir < 0 && stop_point_locked() <= mm_to_steps(CLOSE_SLOW_MM) + s_rezero_tolerance)) {
            start_touch_locked();
        }
        break;
    case CLOSE_TOUCH:
        if (s_endstop_hit) {
            s_close = CLOSE_IDLE; /* closed, position 0 = end stop */
            set_limits(s_vmax_normal, s_accel_normal);
            set_target_locked(0);
        } else if (motion_idle(&s_m)) {
            /* CLOSE_SEEK_MM below 0 and still not pressed: something holds the sleeve up (or steps were lost
             * going down). The position is wrong by the lost steps: not ready (no move trusts it) until homed.
             * Back up rather than keep pushing. The sleeve only went down since s_close_peak: after backing up it
             * is at most RELEASE_MM higher, so homing has a bounded search. */
            int32_t release = mm_to_steps(RELEASE_MM);
            s_close = CLOSE_RELEASE;
            s_ready = false;
            s_lost_top = (s_close_peak > 0 ? s_close_peak : 0) + release;
            set_target_locked(s_m.pos + release);
        }
        break;
    case CLOSE_RELEASE:
        if (motion_idle(&s_m)) {
            /* At rest RELEASE_MM above whatever stopped it, holding like any rest (CONFIG_MOTOR_IDLE_OFF_MS) */
            s_close = CLOSE_IDLE;
            ev = STEPPER_EVENT_NOT_CLOSED;
        }
        break;
    case CLOSE_IDLE:
        break;
    }

    /* The target only changes under s_lock: at rest now means at rest until the next set_target_locked() */
    if (s_running && s_close == CLOSE_IDLE && motion_idle(&s_m)) {
        stop_timer_locked(now);
    }
#if CONFIG_MOTOR_IDLE_OFF_MS > 0
    if (!s_running && s_driver_on && now - s_idle_since_us >= (int64_t)CONFIG_MOTOR_IDLE_OFF_MS * 1000) {
        driver_enable(false);
    }
#endif
    if (ev == STEPPER_EVENT_NONE && s_rezeroed) {
        ev = STEPPER_EVENT_REZEROED;
    }
    s_rezeroed = false;
    xSemaphoreGive(s_lock);
    return ev;
}

void stepper_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stop_timer_locked(esp_timer_get_time());
    s_m.target = s_m.pos;
    motion_halt(&s_m, s_m.pos);
    s_close = CLOSE_IDLE;
    s_ready = false;
#if CONFIG_MOTOR_IDLE_OFF_MS > 0
    driver_enable(false); /* the lead screw holds the sleeve; nothing polls any more to turn the driver off later */
#endif
    xSemaphoreGive(s_lock);
}

void stepper_disable(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stop_timer_locked(esp_timer_get_time());
    s_m.target = s_m.pos;
    motion_halt(&s_m, s_m.pos);
    s_close = CLOSE_IDLE;
    s_ready = false;
    driver_enable(false);
    xSemaphoreGive(s_lock);
}

#endif /* CONFIG_MOTOR_ENABLED */
