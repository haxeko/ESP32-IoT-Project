// Host simulation of the step motor driver (main/stepper.c) driving a simulated lamp:
//  - GPIO registers and gpio_* calls (stubs/soc, stubs/driver/gpio.h), the GPTimer with the ESP-IDF v6.0.2 state
//    machine (stubs/driver/gptimer.h) whose alarm calls the interrupt handler every 50 us of simulated time, and
//    vTaskDelay() (stubs/freertos/task.h) that lets the simulated time run at the firmware's tick rate (100 Hz);
//  - a TMC2209: each STEP rising edge moves the sleeve one microstep in the DIR direction while EN is low;
//  - the sleeve: hard bottom 1 mm below the end stop switching point (switch over-travel), hard top 2 mm above
//    full open; optional lost steps (the motor slips) going up or down;
//  - the end stop (NC by default): pressed at or below 0 with 0.2 mm differential travel, optional contact
//    chatter, short glitches, or a broken wire (reads open).
// Scenarios: homing from half open / full open / closed / resting in the over-travel, broken end stop wire, motor
// not connected, moves after homing (limits, STEP/DIR timing, EN), timer stop and driver off at rest, lost steps
// (found by the end stop: fault, homing again), lost steps or an obstacle while closing (backs up, homes at the next
// command with a bounded search), opening while a failed closing backs up or checks the end stop, how long the
// motor presses on an obstacle, the sleeve held at the top while closing, Off near the slow zone (no turning back),
// end stop wire breaking (open or closed lamp) or a glitch after homing, contact chatter, motor turning the wrong
// way (information only), the "3 in a row" count of failed closings. main/lift.c is compiled in too: the scenarios
// drive its lift_pass() as its task does.
// Built several times by run.sh with different CONFIG_MOTOR_* values (Kconfig defaults below otherwise).
#include "check.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_err.h>

/* main/Kconfig.projbuild defaults */
#ifndef CONFIG_MOTOR_ENABLED
#define CONFIG_MOTOR_ENABLED 1
#endif
#ifndef CONFIG_MOTOR_STEP_GPIO
#define CONFIG_MOTOR_STEP_GPIO 5
#endif
#ifndef CONFIG_MOTOR_DIR_GPIO
#define CONFIG_MOTOR_DIR_GPIO 6
#endif
#ifndef CONFIG_MOTOR_EN_GPIO
#define CONFIG_MOTOR_EN_GPIO 7
#endif
#ifndef CONFIG_MOTOR_ENDSTOP_GPIO
#define CONFIG_MOTOR_ENDSTOP_GPIO 3
#endif
#ifndef CONFIG_MOTOR_ENDSTOP_NC /* run.sh passes 0 for a NO contact (a disabled bool is undefined in sdkconfig.h) */
#define CONFIG_MOTOR_ENDSTOP_NC 1
#endif
#ifndef CONFIG_MOTOR_DIR_INVERT
#define CONFIG_MOTOR_DIR_INVERT 0
#endif
#ifndef CONFIG_MOTOR_STEPS_PER_REV
#define CONFIG_MOTOR_STEPS_PER_REV 200
#endif
#ifndef CONFIG_MOTOR_MICROSTEPS
#define CONFIG_MOTOR_MICROSTEPS 8
#endif
#ifndef CONFIG_MOTOR_LEAD_UM
#define CONFIG_MOTOR_LEAD_UM 8000
#endif
#ifndef CONFIG_MOTOR_TRAVEL_MM
#define CONFIG_MOTOR_TRAVEL_MM 150
#endif
#ifndef CONFIG_MOTOR_SPEED_MM_S
#define CONFIG_MOTOR_SPEED_MM_S 10
#endif
#ifndef CONFIG_MOTOR_ACCEL_MM_S2
#define CONFIG_MOTOR_ACCEL_MM_S2 20
#endif
#ifndef CONFIG_MOTOR_HOMING_SPEED_MM_S
#define CONFIG_MOTOR_HOMING_SPEED_MM_S 8
#endif
#ifndef CONFIG_MOTOR_IDLE_OFF_MS
#define CONFIG_MOTOR_IDLE_OFF_MS 0
#endif
#ifndef CONFIG_LED_GPIO
#define CONFIG_LED_GPIO 4
#endif
#ifndef CONFIG_APP_BUTTON_GPIO
#define CONFIG_APP_BUTTON_GPIO 9
#endif
#ifndef CONFIG_LED_COUNT
#define CONFIG_LED_COUNT 30
#endif

/* On the device a failed ESP_ERROR_CHECK aborts (reboot): here it fails the scenario and ends it */
static jmp_buf g_jmp;
static bool g_jmp_armed;
static void sim_abort(const char *why, const char *file, int line) __attribute__((noreturn));
#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)                                                                                             \
    do {                                                                                                               \
        esp_err_t rc_ = (x);                                                                                           \
        if (rc_ != ESP_OK) {                                                                                           \
            printf("  ESP_ERROR_CHECK(%s) returned 0x%x\n", #x, rc_);                                                  \
            sim_abort("ESP_ERROR_CHECK failed, the device would abort and reboot", __FILE__, __LINE__);               \
        }                                                                                                              \
    } while (0)

#include "stepper.c"

/* Stub globals (esp_timer.h, gptimer.h) */
struct sim_timer g_sim_timers[SIM_MAX_TIMERS];
int g_sim_timer_count;
int64_t g_sim_now_us;
struct gptimer_t g_sim_gptimer;

#define STEPS_PER_MM ((long)CONFIG_MOTOR_STEPS_PER_REV * CONFIG_MOTOR_MICROSTEPS * 1000 / CONFIG_MOTOR_LEAD_UM)
#define TRAVEL ((long)CONFIG_MOTOR_TRAVEL_MM * STEPS_PER_MM)
#define TICK_US (TIMER_RES_HZ / TICK_HZ) /* 50 us */
#define NEVER INT64_MIN
#define MAX_SPS (TICK_HZ / 2)
#define CLAMP_SPS(s) ((s) > MAX_SPS ? MAX_SPS : (s))
#define HOMING_SPS CLAMP_SPS(CONFIG_MOTOR_HOMING_SPEED_MM_S * STEPS_PER_MM)
#define SLOW_SPS (HOMING_SPS / SLOW_TOUCH_DIV) /* a quarter of the (clamped) homing speed, as stepper_home() */
#define SPEED_SPS CLAMP_SPS(CONFIG_MOTOR_SPEED_MM_S * STEPS_PER_MM)
#define ACCEL_SPS2 ((long)CONFIG_MOTOR_ACCEL_MM_S2 * STEPS_PER_MM)
/* Time (ms) to leave the end stop at boot: 3 mm at the homing speed, acceleration and braking, 1 s margin */
#define BACKOFF_BOUND_MS (1000 + mm_to_steps(BACKOFF_MM) * 1000L / HOMING_SPS + 2000L * HOMING_SPS / ACCEL_SPS2 + 40)
/* Steps a closing runs past the switching point before the debounce stops it (at the homing speed) */
#define TOUCH_DEPTH (2 + (long)HOMING_SPS * ENDSTOP_DEBOUNCE * TICK_US / 1000000)

/* ---------------------------------------------------------------------------------------------------------------
 * Simulated hardware
 */
static struct {
    /* GPIO */
    uint32_t out;     /* output latch */
    uint64_t outputs; /* GPIOs configured as outputs */
    uint64_t pullups;
    long bad_reg;
    /* Mechanics */
    bool motor_connected;
    bool wired_reversed; /* the motor turns the other way than MOTOR_DIR_INVERT assumes */
    long phys;           /* sleeve, in steps: 0 = the end stop switches there */
    long base, top;      /* hard bottom (-base) and top: steps beyond are lost (stall) */
    long obstacle;       /* something in the way below this height (0 = none): steps down past it are lost */
    long steps_blocked_obstacle;
    int64_t first_block_us, last_block_us; /* first and last step lost on the obstacle */
    int slip_up_every, slip_down_every; /* the motor loses 1 step in this many (0 = never) */
    long slip_n;
    /* End stop */
    bool wire_broken; /* open circuit: reads as an open contact */
    bool pressed;     /* actuated (with differential travel) */
    long release_above;
    int bounce_ticks, bounce_left; /* chatter after each change of the contact, in timer ticks */
    int glitch_ticks;              /* force "pressed" readings for this many ticks ... */
    long glitch_at;                /* ... when the sleeve passes this position going down */
    int glitch_left;
    uint32_t rng;
    /* Observations */
    bool in_isr;
    long ticks;
    long steps, steps_up, steps_disabled, steps_blocked_bottom, steps_blocked_top, steps_slipped, steps_wrong_dir;
    long steps_outside_isr, moving_while_disabled_ticks;
    long min_phys, max_phys;
    int64_t last_rise_us, last_fall_us, last_dir_us, en_on_us;
    int64_t min_gap_us, last_gap_us, min_high_us, min_low_us, min_dir_setup_us;
    int64_t min_first_step_us; /* EN on -> first STEP */
    bool first_step_pending;
    long closing_rest_ticks; /* ticks at rest above 0 while closing (before the end stop) */
    /* What main/lift.c did (its stepper calls go through counting wrappers) */
    int rezero_reports, faults, not_closed;
    esp_err_t rehome_err;
    int rehomes, refused;
    uint16_t exposed;   /* led_bar_set_exposed() */
    bool follow_level;  /* led_bar_follow_level() */
    long target;        /* wanted target (what the Matter state asks for) */
    bool has_target;
    uint32_t next_wait_ms; /* lift_pass(): time to the next pass */
    int64_t limit_us; /* simulated time limit of a scenario (hang guard) */
} g;

static void sim_abort(const char *why, const char *file, int line)
{
    g_failures++;
    printf("  FAIL %s:%d: %s\n", file, line, why);
    if (g_jmp_armed) {
        longjmp(g_jmp, 1);
    }
    abort();
}

static bool en_active(void)
{
#if CONFIG_MOTOR_EN_GPIO >= 0
    if (!(g.outputs & BIT64(CONFIG_MOTOR_EN_GPIO))) {
        return false; /* not driven: the 10 kOhm pull-up of the README keeps the driver off */
    }
    return !(g.out & BIT(CONFIG_MOTOR_EN_GPIO));
#else
    return true; /* EN not connected: the driver is always on */
#endif
}

/* Driver output off (always on when EN is not connected) */
#if CONFIG_MOTOR_EN_GPIO >= 0
#define EN_OFF() (!en_active())
#else
#define EN_OFF() true
#endif

static void contact_update(void)
{
    bool p = g.pressed ? g.phys <= g.release_above : g.phys <= 0;
    if (p != g.pressed) {
        g.pressed = p;
        g.bounce_left = g.bounce_ticks;
    }
}

static bool endstop_pin_high(void)
{
    if (g.wire_broken) {
        return true; /* open circuit: the pull-up wins */
    }
    bool pressed = g.pressed || g.glitch_left > 0;
    if (g.bounce_left > 0) {
        g.rng = g.rng * 1103515245u + 12345u;
        if ((g.rng >> 16) & 1) {
            pressed = !pressed;
        }
    }
#if CONFIG_MOTOR_ENDSTOP_NC
    return pressed; /* NC to GND opens when pressed */
#else
    return !pressed; /* NO to GND closes when pressed */
#endif
}

static void sim_step(void)
{
    int64_t now = g_sim_now_us;
    g.steps++;
    if (!g.in_isr) {
        g.steps_outside_isr++;
    }
    if (g.last_rise_us != NEVER) {
        g.last_gap_us = now - g.last_rise_us;
        if (g.last_gap_us < g.min_gap_us) {
            g.min_gap_us = g.last_gap_us;
        }
    }
    if (g.last_fall_us != NEVER && g.last_fall_us >= g.last_rise_us && now - g.last_fall_us < g.min_low_us) {
        g.min_low_us = now - g.last_fall_us;
    }
    if (g.last_dir_us != NEVER && g.last_dir_us >= g.last_rise_us && now - g.last_dir_us < g.min_dir_setup_us) {
        g.min_dir_setup_us = now - g.last_dir_us;
    }
    g.last_rise_us = now;
    if (g.first_step_pending) {
        int64_t d = now - g.en_on_us;
        g.min_first_step_us = d < g.min_first_step_us ? d : g.min_first_step_us;
        g.first_step_pending = false;
    }

    bool up = ((g.out & BIT(CONFIG_MOTOR_DIR_GPIO)) != 0) != (CONFIG_MOTOR_DIR_INVERT != 0);
    if (g.wired_reversed) {
        up = !up;
    } else if ((up ? 1 : -1) != s_m.dir) {
        g.steps_wrong_dir++; /* DIR pin differs from the direction the planner counted this step in */
    }
    if (up) {
        g.steps_up++;
    }
    if (!en_active()) {
        g.steps_disabled++;
        return;
    }
    if (!g.motor_connected) {
        return;
    }
    int every = up ? g.slip_up_every : g.slip_down_every;
    if (every && ++g.slip_n % every == 0) {
        g.steps_slipped++;
        return;
    }
    long np = g.phys + (up ? 1 : -1);
    if (!up && g.obstacle && np < g.obstacle) {
        if (g.steps_blocked_obstacle++ == 0) {
            g.first_block_us = now;
        }
        g.last_block_us = now;
        return;
    }
    if (np < -g.base) {
        g.steps_blocked_bottom++;
        return;
    }
    if (np > g.top) {
        g.steps_blocked_top++;
        return;
    }
    if (!up && g.glitch_ticks && g.phys > g.glitch_at && np <= g.glitch_at) {
        g.glitch_left = g.glitch_ticks;
    }
    g.phys = np;
    g.min_phys = np < g.min_phys ? np : g.min_phys;
    g.max_phys = np > g.max_phys ? np : g.max_phys;
    contact_update();
}

static void sim_set_out(uint32_t v)
{
    bool en_before = en_active();
    uint32_t changed = g.out ^ v;
    g.out = v;
    if ((changed & BIT(CONFIG_MOTOR_DIR_GPIO)) && (g.outputs & BIT64(CONFIG_MOTOR_DIR_GPIO))) {
        g.last_dir_us = g_sim_now_us;
    }
    if ((changed & BIT(CONFIG_MOTOR_STEP_GPIO)) && (g.outputs & BIT64(CONFIG_MOTOR_STEP_GPIO))) {
        if (v & BIT(CONFIG_MOTOR_STEP_GPIO)) {
            sim_step();
        } else {
            if (g.last_rise_us != NEVER && g_sim_now_us - g.last_rise_us < g.min_high_us) {
                g.min_high_us = g_sim_now_us - g.last_rise_us;
            }
            g.last_fall_us = g_sim_now_us;
        }
    }
    if (!en_before && en_active()) {
        g.en_on_us = g_sim_now_us;
        g.first_step_pending = true;
    }
}

uint32_t sim_reg_read(uint32_t reg)
{
    if (reg != GPIO_IN_REG) {
        g.bad_reg++;
        return 0;
    }
    uint32_t in = g.out & (uint32_t)g.outputs;
    if (endstop_pin_high()) {
        in |= BIT(CONFIG_MOTOR_ENDSTOP_GPIO);
    } else {
        in &= ~(uint32_t)BIT(CONFIG_MOTOR_ENDSTOP_GPIO);
    }
    return in;
}

void sim_reg_write(uint32_t reg, uint32_t val)
{
    if (reg == GPIO_OUT_W1TS_REG) {
        sim_set_out(g.out | val);
    } else if (reg == GPIO_OUT_W1TC_REG) {
        sim_set_out(g.out & ~val);
    } else {
        g.bad_reg++;
    }
}

esp_err_t sim_gpio_config(const gpio_config_t *cfg)
{
    if (cfg->pin_bit_mask == 0 || (cfg->pin_bit_mask >> 22) != 0) {
        return ESP_ERR_INVALID_ARG; /* ESP32-C3: GPIO0..21 */
    }
    bool en_before = en_active();
    if (cfg->mode & GPIO_MODE_OUTPUT) {
        g.outputs |= cfg->pin_bit_mask;
    } else {
        g.outputs &= ~cfg->pin_bit_mask;
    }
    if (cfg->pull_up_en) {
        g.pullups |= cfg->pin_bit_mask;
    } else {
        g.pullups &= ~cfg->pin_bit_mask;
    }
    if (!en_before && en_active()) {
        g.en_on_us = g_sim_now_us;
        g.first_step_pending = true;
    }
    return ESP_OK;
}

esp_err_t sim_gpio_set_level(int gpio, uint32_t level)
{
    if (gpio < 0 || gpio > 21) {
        return ESP_ERR_INVALID_ARG;
    }
    sim_set_out(level ? g.out | BIT(gpio) : g.out & ~(uint32_t)BIT(gpio));
    return ESP_OK;
}

/* One timer alarm: the interrupt */
static void sim_isr(void)
{
    g.ticks++;
    if (g.bounce_left > 0) {
        g.bounce_left--;
    }
    const gptimer_alarm_event_data_t ev = {.count_value = g_sim_gptimer.alarm_count,
                                           .alarm_value = g_sim_gptimer.alarm_count};
    g.in_isr = true;
    g_sim_gptimer.on_alarm(&g_sim_gptimer, &ev, g_sim_gptimer.user_ctx);
    g.in_isr = false;
    if (g.glitch_left > 0) {
        g.glitch_left--;
    }
    if (s_m.vel != 0 && !en_active()) {
        g.moving_while_disabled_ticks++;
    }
    if ((s_close == CLOSE_APPROACH || s_close == CLOSE_TOUCH) && s_m.vel == 0 && s_m.pos > 0) {
        g.closing_rest_ticks++;
    }
}

/* Let the simulated time run; the timer counts at 1 MHz (checked in boot()) */
static void sim_run_us(int64_t us)
{
    struct gptimer_t *t = &g_sim_gptimer;
    while (us > 0) {
        if (t->fsm != SIM_GPTIMER_RUN || !t->alarm_set || !t->on_alarm) {
            g_sim_now_us += us;
            return;
        }
        int64_t to_alarm = (int64_t)(t->alarm_count - t->count);
        if (to_alarm > us) {
            t->count += (uint64_t)us;
            g_sim_now_us += us;
            return;
        }
        g_sim_now_us += to_alarm;
        us -= to_alarm;
        t->count = t->reload_count;
        sim_isr();
    }
}

void sim_task_delay(TickType_t ticks)
{
    sim_run_us((int64_t)ticks * (1000000 / configTICK_RATE_HZ));
    if (g_sim_now_us > g.limit_us) {
        sim_abort("simulated time limit reached (hang?)", __FILE__, __LINE__);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * main/lift.c, compiled here. Its stepper calls go through wrappers that count what happened; led_bar is faked.
 */
#include "led_bar.h"

static stepper_event_t sim_stepper_poll(void)
{
    stepper_event_t ev = stepper_poll();
    if (ev == STEPPER_EVENT_REZEROED) {
        g.rezero_reports++;
    } else if (ev == STEPPER_EVENT_FAULT) {
        g.faults++;
    } else if (ev == STEPPER_EVENT_NOT_CLOSED) {
        g.not_closed++;
    }
    return ev;
}

static esp_err_t sim_stepper_home(void)
{
    g.rehomes++;
    g.rehome_err = stepper_home();
    return g.rehome_err;
}

static bool sim_stepper_move_to(int32_t pos)
{
    bool accepted = stepper_move_to(pos);
    if (!accepted) {
        g.refused++;
    }
    return accepted;
}

uint16_t led_bar_count_for_level(uint8_t level)
{
    return level; /* not used: the scenarios give targets in steps */
}

void led_bar_set_exposed(uint16_t count)
{
    g.exposed = count;
    g.follow_level = false; /* as led_bar.c: back to lift mode */
}

void led_bar_follow_level(void)
{
    g.follow_level = true;
}

void led_bar_set_target_callback(led_bar_target_cb_t cb)
{
    (void)cb;
}

#define stepper_poll    sim_stepper_poll
#define stepper_home    sim_stepper_home
#define stepper_move_to sim_stepper_move_to
#define TAG             LIFT_TAG
#include "lift.c"
#undef stepper_poll
#undef stepper_home
#undef stepper_move_to
#undef TAG

/* One pass of the lift task (main/lift.c lift_task()) for the wanted target. Before any target (the firmware starts
 * with Off), only the stepper housekeeping, none once given up. */
static void lift_iteration(void)
{
    if (!g.has_target) {
        if (!s_stopped) {
            (void)sim_stepper_poll();
        }
        g.next_wait_ms = stepper_busy() ? 20 : 250;
        return;
    }
    g.next_wait_ms = lift_pass((int32_t)g.target, 0);
}

/* The lift task waits until its next pass */
static void lift_wait(void)
{
    vTaskDelay(pdMS_TO_TICKS(g.next_wait_ms ? g.next_wait_ms : 250));
}

static bool lift_pending(void)
{
    return !s_stopped && g.has_target && g.target != s_last_target;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Helpers
 */

/* Power on with the sleeve at `phys`: fresh hardware, fresh driver statics (as after a reset), stepper_init() */
static void boot(long phys)
{
    memset(&g, 0, sizeof g);
    g.motor_connected = true;
    g.phys = g.min_phys = g.max_phys = phys;
    g.base = STEPS_PER_MM;                 /* 1 mm of switch over-travel, then the base */
    g.top = TRAVEL + 2 * STEPS_PER_MM;     /* end of the lead screw 2 mm above full open */
    g.release_above = STEPS_PER_MM / 5;    /* 0.2 mm differential travel */
    g.pressed = phys <= 0;
    g.rng = 1;
    g.last_rise_us = g.last_fall_us = g.last_dir_us = NEVER;
    g.min_gap_us = g.min_high_us = g.min_low_us = g.min_dir_setup_us = g.min_first_step_us = INT64_MAX;
    g.limit_us = 600LL * 1000 * 1000;
    /* main/lift.c statics, as after a reset */
    s_task = NULL;
    s_want = 0;
    s_last_target = -1;
    s_not_closed = 0;
    s_stopped = false;

    memset(&g_sim_gptimer, 0, sizeof g_sim_gptimer);
    g_sim_now_us = 0;

    s_m = (motion_t){0};
    s_timer = NULL;
    memset(&s_lock_buf, 0, sizeof s_lock_buf);
    s_lock = NULL;
    s_running = false;
    s_driver_on = false;
    s_idle_since_us = 0;
    s_vmax_normal = s_accel_normal = 0;
    s_rezero_tolerance = 0;
    s_vmax_homing = s_accel_sps2 = 0;
    s_window = 0;
    s_homed = s_ready = false;
    s_lost = false;
    s_rise = 0;
    s_close = CLOSE_IDLE;
    s_homing = s_fault = false;
    s_step_high = false;
    s_dir_out = 0;
    s_endstop_count = 0;
    s_endstop_hit = false;
    s_rezeroed = false;

    CHECK(stepper_init() == ESP_OK);
    CHECK(g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE && g_sim_gptimer.resolution_hz == 1000000);
    CHECK(g_sim_gptimer.alarm_set && g_sim_gptimer.auto_reload && g_sim_gptimer.alarm_count == TICK_US);
    CHECK(EN_OFF() && (g.out & BIT(CONFIG_MOTOR_STEP_GPIO)) == 0);
    CHECK(g.pullups & BIT64(CONFIG_MOTOR_ENDSTOP_GPIO));
    CHECK(stepper_travel_steps() == TRAVEL);
}

static int64_t home(esp_err_t *err)
{
    int64_t t0 = g_sim_now_us;
    *err = stepper_home();
    return (g_sim_now_us - t0) / 1000;
}

/* A new wanted target (the Matter callback wakes the lift task: one pass at once) */
static void lift_iteration(void);
static void lift_move(long target)
{
    g.target = target;
    g.has_target = true;
    lift_iteration();
}

/* The lift task (lift.c) after a new target: poll every 20 ms while moving, 250 ms at rest. Returns the time (ms)
 * until the motor is at rest with the timer interrupt stopped, -1 if not within max_ms. */
static int64_t settle(int64_t max_ms)
{
    int64_t t0 = g_sim_now_us;
    for (;;) {
        lift_wait();
        lift_iteration();
        if (!stepper_busy() && !lift_pending() && g_sim_gptimer.fsm != SIM_GPTIMER_RUN) {
            return (g_sim_now_us - t0) / 1000;
        }
        if (g_sim_now_us - t0 > max_ms * 1000) {
            return -1;
        }
    }
}

static void wait_ms(int64_t ms) /* the lift task keeps running */
{
    int64_t end = g_sim_now_us + ms * 1000;
    while (g_sim_now_us < end) {
        lift_wait();
        lift_iteration();
    }
}

/* Run the lift task until cond() holds (checked every 10 ms), at most max_ms: false if it never did */
static bool run_until(bool (*cond)(void), int64_t max_ms)
{
    int64_t end = g_sim_now_us + max_ms * 1000;
    int64_t next_pass = g_sim_now_us;
    while (g_sim_now_us < end) {
        if (cond()) {
            return true;
        }
        if (g_sim_now_us >= next_pass) {
            lift_iteration();
            next_pass = g_sim_now_us + 20000;
        }
        vTaskDelay(1); /* 10 ms */
    }
    return cond();
}

/* How long the motor pressed on the obstacle: first to last step lost on it (ms) */
static int64_t press_ms(void)
{
    return g.steps_blocked_obstacle ? (g.last_block_us - g.first_block_us) / 1000 : 0;
}

static void press_reset(void)
{
    g.steps_blocked_obstacle = 0;
    g.first_block_us = g.last_block_us = 0;
}

/* Driver state after the lamp stopped for good or after a failed closing: holding when Driver off at rest is 0 */
#define DRIVER_HOLDS_AT_REST() (CONFIG_MOTOR_IDLE_OFF_MS > 0 || en_active())

static int64_t move_ms(long steps) /* generous time for a move at the normal speed, with acceleration and braking */
{
    return (int64_t)labs(steps) * 1000 / SPEED_SPS + 2000LL * SPEED_SPS / ACCEL_SPS2 + 5000;
}

/* STEP / DIR / EN signal rules for a TMC2209, and the driver's own bookkeeping */
static void check_signals(void)
{
    CHECK(g.min_gap_us == INT64_MAX || g.min_gap_us >= 2 * TICK_US); /* <= 10 kHz, one tick high, one low */
    CHECK(g.min_high_us == INT64_MAX || g.min_high_us >= TICK_US);
    CHECK(g.min_low_us == INT64_MAX || g.min_low_us >= TICK_US);
    CHECK(g.min_dir_setup_us == INT64_MAX || g.min_dir_setup_us >= TICK_US); /* DIR never set in the step's tick */
    CHECK(g.steps_wrong_dir == 0);
    CHECK(g.steps_disabled == 0);              /* EN asserted at every step */
    CHECK(g.moving_while_disabled_ticks == 0); /* ... and whenever the planner moves */
    CHECK(g.steps_outside_isr == 0);
    CHECK(g.bad_reg == 0);
    CHECK(g_sim_gptimer.redundant_starts == 0 && g_sim_gptimer.redundant_stops == 0);
}

/* Home and check position 0: within one step of the switching point, the fast touch at most 2 steps past it (the
 * debounce). Contact chatter (chatter_ms) delays both stops by up to its duration. */
static bool home_ok_at(long start, const char *what, int chatter_ms)
{
    /* steps run during the chatter and the debounce (ENDSTOP_DEBOUNCE ticks) after the switching point */
    long deeper = 2 + (long)HOMING_SPS * (chatter_ms * 1000L + ENDSTOP_DEBOUNCE * TICK_US) / 1000000;
    long final = 1 + (long)SLOW_SPS * (chatter_ms * 1000L + ENDSTOP_DEBOUNCE * TICK_US) / 1000000;
    esp_err_t err;
    int64_t ms = home(&err);
    printf("  homing from %s (%.1f mm): %s in %lld ms, sleeve %ld steps from the switching point, deepest %ld\n", what,
           (double)start / STEPS_PER_MM, err == ESP_OK ? "OK" : "FAILED", (long long)ms, g.phys, g.min_phys);
    CHECK(err == ESP_OK);
    CHECK(stepper_position() == 0);
    CHECK(g.phys <= 0 && g.phys >= -final); /* position 0 = where the end stop switches, within one step */
    CHECK(g.pressed);
    CHECK(g.min_phys >= (start < -deeper ? start : -deeper)); /* the touch stops within the debounce */
    CHECK(g.steps_blocked_bottom == 0 && g.steps_blocked_top == 0);
    if (err == ESP_OK && g.last_gap_us > 0) {
        /* the last approach ran at (at most) a quarter of the homing speed */
        CHECK(g.last_gap_us >= 1000000 / SLOW_SPS - TICK_US);
    }
    check_signals();
    return err == ESP_OK;
}

static bool home_ok_at_zero(long start, const char *what)
{
    return home_ok_at(start, what, 0);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Scenarios
 */

static void scenario_home_half_open(void)
{
    boot(TRAVEL / 2);
    home_ok_at_zero(TRAVEL / 2, "half open");
    CHECK(g.max_phys == TRAVEL / 2); /* never went up first */
    CHECK(settle(1000) >= 0);        /* the next poll stops the timer interrupt */
    CHECK(g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE);
    if (g.min_first_step_us != INT64_MAX) {
        printf("  EN on -> first STEP: %lld us\n", (long long)g.min_first_step_us);
    }
}

static void scenario_home_full_open(void)
{
    boot(TRAVEL);
    /* With a homing speed above 10000 steps/s (run.sh build stepper_fine: 2 mm lead, 32 microsteps) the timeouts
     * must use the clamped speed */
    home_ok_at_zero(TRAVEL, "full open");
}

static void scenario_home_closed(void)
{
    boot(0);
    home_ok_at_zero(0, "closed, end stop pressed");
    CHECK(g.max_phys >= mm_to_steps(BACKOFF_MM) - 1); /* left the end stop first */

    boot(-STEPS_PER_MM / 2); /* resting in the switch over-travel (e.g. slid down with the driver off) */
    home_ok_at_zero(-STEPS_PER_MM / 2, "inside the end stop over-travel");
}

static void scenario_home_chatter(void)
{
    boot(TRAVEL / 3);
    g.bounce_ticks = 40; /* 2 ms of contact chatter after each change */
    home_ok_at(TRAVEL / 3, "a third open, 2 ms contact chatter", 2);
    CHECK(settle(1000) >= 0);
    long zero = g.phys;
    /* A short glitch (< debounce) going down must not stop the motion */
    g.bounce_ticks = 0;
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    g.glitch_ticks = ENDSTOP_DEBOUNCE - 1;
    g.glitch_at = TRAVEL / 4;
    lift_move(TRAVEL / 8);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    CHECK(g.rezero_reports == 0 && stepper_position() == TRAVEL / 8 && labs(g.phys - (TRAVEL / 8 + zero)) <= 1);
    check_signals();
}

static void scenario_broken_wire_at_boot(void)
{
    boot(TRAVEL / 2);
    g.wire_broken = true;
    esp_err_t err;
    int64_t ms = home(&err);
    printf("  end stop wire broken (%s): err 0x%x after %lld ms, sleeve moved %+ld steps\n",
           CONFIG_MOTOR_ENDSTOP_NC ? "NC: reads pressed" : "NO: reads released", err, (long long)ms,
           g.max_phys - TRAVEL / 2 + (g.min_phys - TRAVEL / 2));
#if CONFIG_MOTOR_ENDSTOP_NC
    CHECK(err == ESP_ERR_INVALID_STATE);
    CHECK(ms <= BACKOFF_BOUND_MS);
    CHECK(g.min_phys == TRAVEL / 2 && g.max_phys <= TRAVEL / 2 + mm_to_steps(BACKOFF_MM)); /* only the 3 mm up */
#else
    CHECK(err == ESP_ERR_NOT_FOUND); /* NO: a broken wire is never pressed (why NC is recommended) */
    printf("  NO contact, broken wire: %ld steps pushed against the base\n", g.steps_blocked_bottom);
#endif
    stepper_disable();
    CHECK(EN_OFF() && g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE && !stepper_busy());
    long steps = g.steps;
    wait_ms(1000);
    CHECK(g.steps == steps);
    check_signals();
}

static void scenario_motor_missing(void)
{
    boot(TRAVEL / 2);
    g.motor_connected = false;
    esp_err_t err;
    int64_t ms = home(&err);
    printf("  motor not connected, half open: err 0x%x after %lld ms (%ld STEP pulses)\n", err, (long long)ms,
           g.steps);
    CHECK(err == ESP_ERR_NOT_FOUND);
    /* the whole search (125 % of the travel) at the homing speed, the 10 mm back-up, then it gives up */
    CHECK(ms <= (TRAVEL + TRAVEL / 4 + mm_to_steps(RELEASE_MM)) * 1000L / HOMING_SPS + 4000L * HOMING_SPS / ACCEL_SPS2 +
                    3000);
    stepper_disable();
    CHECK(EN_OFF() && g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE);
    check_signals();

    boot(0);
    g.motor_connected = false;
    ms = home(&err);
    printf("  motor not connected, closed: err 0x%x after %lld ms\n", err, (long long)ms);
    CHECK(err == ESP_ERR_INVALID_STATE);
    CHECK(ms <= BACKOFF_BOUND_MS);
    stepper_disable();
    CHECK(EN_OFF());
}

static void scenario_moves(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    long zero = g.phys; /* physical position of position 0 */
    g.min_phys = g.max_phys = g.phys;

    lift_move(TRAVEL);
    CHECK(en_active());
    int64_t ms = settle(move_ms(TRAVEL));
    printf("  0 -> full open: %lld ms\n", (long long)ms);
    CHECK(ms >= 0 && stepper_position() == TRAVEL && g.phys == TRAVEL + zero);

    lift_move(TRAVEL + 1000); /* clamped */
    CHECK(settle(1000) >= 0 && stepper_position() == TRAVEL);
    lift_move(-1000); /* clamped: closes onto the end stop */
    CHECK(settle(move_ms(TRAVEL)) >= 0 && stepper_position() == 0 && g.pressed);
    CHECK(g.rezero_reports == 0 && g.faults == 0 && g.not_closed == 0);
    zero = g.phys;

    /* Many target changes while moving, at random times */
    uint32_t r = 7;
    for (int i = 0; i < 60; i++) {
        r = r * 1103515245u + 12345u;
        long target = (long)((r >> 8) % (uint32_t)(TRAVEL + 1));
        if (i % 7 == 3) {
            target = 0;
        }
        lift_move(target);
        r = r * 1103515245u + 12345u;
        int64_t end = g_sim_now_us + (int64_t)((r >> 8) % 1500) * 1000;
        while (g_sim_now_us < end) {
            lift_wait();
            lift_iteration();
        }
        CHECK(labs(g.phys - (stepper_position() + zero)) <= TOUCH_DEPTH);
    }
    lift_move(TRAVEL / 3);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    CHECK(stepper_position() == TRAVEL / 3 && labs(g.phys - TRAVEL / 3) <= TOUCH_DEPTH);
    CHECK(g.min_phys >= -TOUCH_DEPTH && g.max_phys <= TRAVEL); /* never outside 0..travel */
    CHECK(g.steps_blocked_bottom == 0 && g.steps_blocked_top == 0);
    CHECK(g.rezero_reports == 0 && g.faults == 0 && g.not_closed == 0);
    check_signals();
}

static void scenario_rest(void)
{
    boot(TRAVEL / 4);
    if (!home_ok_at_zero(TRAVEL / 4, "a quarter open")) {
        return;
    }
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    int starts = g_sim_gptimer.starts;
    CHECK(g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE); /* interrupt stopped at rest */
    long ticks = g.ticks;
#if CONFIG_MOTOR_IDLE_OFF_MS > 0 && CONFIG_MOTOR_EN_GPIO >= 0
    CHECK(en_active());
    wait_ms(CONFIG_MOTOR_IDLE_OFF_MS - 300);
    CHECK(en_active()); /* still holding */
    wait_ms(600);
    CHECK(!en_active()); /* then off */
#else
    wait_ms(10000);
    CHECK(en_active()); /* keeps holding */
#endif
    CHECK(g.ticks == ticks); /* no interrupt at rest */
    /* The next move turns the driver on before the first step and restarts the timer once */
    lift_move(TRAVEL / 4);
    CHECK(en_active() && g_sim_gptimer.fsm == SIM_GPTIMER_RUN && g_sim_gptimer.starts == starts + 1);
    CHECK(settle(move_ms(TRAVEL)) >= 0 && labs(g.phys - TRAVEL / 4) <= TOUCH_DEPTH);
    lift_move(TRAVEL / 4); /* same target: no motion, the poll stops the timer again */
    CHECK(settle(1000) >= 0 && g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE);
    check_signals();
}

static void scenario_lost_steps_up(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    /* Going up the motor loses 1 step in 10: closing meets the end stop 15 mm early, far above 0: that is a fault
     * (it could be a broken wire), the lift homes again, which finds the end stop really pressed and releasing */
    g.slip_up_every = 10;
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    g.slip_up_every = 0;
    long lost = g.steps_slipped;
    printf("  lost %ld steps going up (%.1f mm)\n", lost, (double)lost / STEPS_PER_MM);
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    printf("  closing: %d fault(s), homed again: %s\n", g.faults, g.rehome_err == ESP_OK ? "OK" : "FAILED");
    CHECK(g.faults == 1 && g.rehome_err == ESP_OK && !s_stopped);
    CHECK(stepper_position() == 0 && g.phys <= 0 && g.phys >= -TOUCH_DEPTH && g.pressed);
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0 && labs(g.phys - TRAVEL / 2) <= TOUCH_DEPTH);

    /* Same, but the new target is low (3 mm): fault on the way down, homing, then back up to the target */
    g.faults = 0;
    g.slip_up_every = 10;
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    g.slip_up_every = 0;
    lift_move(TRAVEL / 50);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    CHECK(g.faults == 1 && g.rehome_err == ESP_OK);
    CHECK(stepper_position() == TRAVEL / 50 && labs(g.phys - TRAVEL / 50) <= TOUCH_DEPTH);
    CHECK(g.min_phys >= -TOUCH_DEPTH && g.max_phys <= TRAVEL && g.steps_blocked_top == 0);
    check_signals();
}

static void scenario_lost_steps_closing(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    /* Something brakes the sleeve while it closes: the motor loses 1 step in 10 going down */
    g.slip_down_every = 10;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    g.slip_down_every = 0;
    printf("  closing with lost steps: %d not closed, sleeve %.1f mm above the end stop, driver %s\n", g.not_closed,
           (double)g.phys / STEPS_PER_MM, EN_OFF() ? "off" : "on");
    /* 6.5 mm lost on the way down, more than the 5 mm searched below 0: backs up 10 mm and holds there */
    CHECK(g.not_closed == 1 && !stepper_ready() && !stepper_busy());
    CHECK(en_active());
    CHECK(g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE);
    /* The next command homes first (bounded search: moved up 75 + 10 mm since the last zero), then opens: never
     * past full open */
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    CHECK(g.rehome_err == ESP_OK && stepper_position() == TRAVEL);
    CHECK(g.max_phys <= TRAVEL && g.steps_blocked_top == 0);

    /* A small loss (1 step in 50: 1.3 mm) is found by the slow end of the closing and corrected */
    g.not_closed = 0;
    g.slip_down_every = 50;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    g.slip_down_every = 0;
    CHECK(g.not_closed == 0 && g.faults == 0 && g.rezero_reports == 1);
    CHECK(stepper_position() == 0 && g.pressed && g.phys >= -TOUCH_DEPTH);
    check_signals();
}

static void scenario_obstacle_closing(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    /* A finger / an object 30 mm above the base while closing */
    g.obstacle = 30 * STEPS_PER_MM;
    int64_t t0 = g_sim_now_us;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    printf("  obstacle at 30 mm: %d not closed after %lld ms, pressed on it for %lld ms (%ld steps lost), sleeve "
           "backed to %.1f mm, driver %s\n",
           g.not_closed, (long long)(g_sim_now_us - t0) / 1000, (long long)press_ms(), g.steps_blocked_obstacle,
           (double)g.phys / STEPS_PER_MM, EN_OFF() ? "off" : "on");
    CHECK(g.not_closed == 1 && en_active()); /* holds 10 mm above it (Driver off at rest applies later) */
    CHECK(g.phys >= g.obstacle + mm_to_steps(RELEASE_MM) - 1); /* backed away from it */
    CHECK(g.max_phys <= TRAVEL);
#if CONFIG_MOTOR_IDLE_OFF_MS > 0 && CONFIG_MOTOR_EN_GPIO >= 0
    wait_ms(CONFIG_MOTOR_IDLE_OFF_MS + 500);
    CHECK(EN_OFF());
#endif
    /* Removed; the next command homes and works */
    g.obstacle = 0;
    lift_move(TRAVEL / 4);
    CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    CHECK(g.rehome_err == ESP_OK && labs(g.phys - TRAVEL / 4) <= TOUCH_DEPTH && !s_stopped);
    /* Blocked three times: gives up (motor off) until a restart */
    for (int i = 0; i < 2 && !s_stopped; i++) {
        g.obstacle = 20 * STEPS_PER_MM;
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        g.obstacle = 0;
        lift_move(TRAVEL / 4);
        CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    }
    printf("  %d failed closings: %s\n", g.not_closed, s_stopped ? "gave up, motor stopped" : "still trying");
    CHECK(g.not_closed == 3 && s_stopped && g.follow_level && (CONFIG_MOTOR_IDLE_OFF_MS > 0 ? EN_OFF() : en_active()));
    long steps = g.steps;
    lift_move(TRAVEL / 2); /* ignored until a restart */
    CHECK(settle(1000) >= 0 && g.steps == steps && g.follow_level);
    check_signals();
}

static void scenario_wire_breaks_after_homing(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    /* The end stop wire comes loose while the lamp is half open */
    g.wire_broken = true;
    lift_move(TRAVEL / 10);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    printf("  wire broken, half open -> 10 %%: position %ld, sleeve %.1f mm, %d fault(s), homing again: 0x%x, "
           "gave up: %s\n",
           (long)stepper_position(), (double)g.phys / STEPS_PER_MM, g.faults, g.rehome_err, s_stopped ? "yes" : "no");
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
#if CONFIG_MOTOR_ENDSTOP_NC
    /* NC: the broken wire reads pressed far above 0: fault, homing again finds it stuck, motor off */
    CHECK(g.faults == 1 && g.rehome_err == ESP_ERR_INVALID_STATE && s_stopped && g.follow_level);
    /* stopped where it is: holding (the sleeve must not slide down) unless Driver off at rest is set */
    CHECK(CONFIG_MOTOR_IDLE_OFF_MS > 0 ? EN_OFF() : en_active());
    CHECK(g.max_phys <= TRAVEL / 2 + mm_to_steps(BACKOFF_MM)); /* at most the 3 mm test move up */
#else
    /* NO: a broken wire reads released; closing then never finds the end stop (covered by other scenarios) */
    CHECK(g.max_phys <= TRAVEL);
#endif
    CHECK(g.steps_blocked_top == 0);
}

static void scenario_wire_breaks_closed(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL)) >= 0 && g.pressed);
    g.min_phys = g.max_phys = g.phys;
    /* The wire breaks while the lamp is closed, then the lamp is opened */
    g.wire_broken = true;
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    printf("  wire broken while closed, then opened: sleeve %.1f mm, %d fault(s), homing again: 0x%x\n",
           (double)g.max_phys / STEPS_PER_MM, g.faults, g.rehome_err);
#if CONFIG_MOTOR_ENDSTOP_NC
    /* It does not release when leaving 0: fault a little above the 3 mm window, homing fails, motor off */
    CHECK(g.faults == 1 && g.rehome_err == ESP_ERR_INVALID_STATE && s_stopped && g.follow_level);
    CHECK(g.max_phys <= 2 * mm_to_steps(ZERO_WINDOW_MM) + mm_to_steps(BACKOFF_MM));
#endif
}

static void scenario_endstop_glitch_after_homing(void)
{
    boot(TRAVEL / 2);
    if (!home_ok_at_zero(TRAVEL / 2, "half open")) {
        return;
    }
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    /* A disturbance on the end stop input just longer than the debounce while closing to 10 % */
    g.glitch_ticks = ENDSTOP_DEBOUNCE + 1;
    g.glitch_at = TRAVEL / 3;
    lift_move(TRAVEL / 10);
    CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
    g.glitch_ticks = 0;
    printf("  %d us glitch at %.1f mm while closing: position %ld, sleeve %.1f mm, %d fault(s), homing again: 0x%x\n",
           (ENDSTOP_DEBOUNCE + 1) * TICK_US, (double)(TRAVEL / 3) / STEPS_PER_MM, (long)stepper_position(),
           (double)g.phys / STEPS_PER_MM, g.faults, g.rehome_err);
    /* Not trusted: fault, homing again (the end stop is fine), back to the target */
    CHECK(g.faults == 1 && g.rehome_err == ESP_OK && stepper_position() == TRAVEL / 10);
    CHECK(labs(g.phys - TRAVEL / 10) <= TOUCH_DEPTH);
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    CHECK(g.max_phys <= TRAVEL && g.steps_blocked_top == 0);
}

/* Home from closed, then open to `pos` */
static bool ready_at(long pos)
{
    boot(0);
    if (!home_ok_at_zero(0, "closed")) {
        return false;
    }
    lift_move(pos);
    if (settle(move_ms(TRAVEL)) < 0 || stepper_position() != pos) {
        CHECK(!"could not open");
        return false;
    }
    g.min_phys = g.max_phys = g.phys;
    return true;
}

static bool counted_below_zero(void)
{
    return s_close == CLOSE_TOUCH && stepper_position() < -mm_to_steps(1);
}

static bool counted_in_slow_zone(void)
{
    return s_close == CLOSE_TOUCH && stepper_position() > 0 && stepper_position() < mm_to_steps(CLOSE_SLOW_MM) / 2;
}

static bool closing_failed(void)
{
    return g.not_closed > 0;
}

static bool backing_up(void)
{
    return s_close == CLOSE_RELEASE && stepper_position() > -mm_to_steps(CLOSE_SEEK_MM) + mm_to_steps(2);
}

static void scenario_open_while_closing_fails(void)
{
    /* Half open, Off with something at 30 mm: the closing loses steps on it, counts into the slow zone and below 0,
     * backs up. On arrives while it checks the end stop (counted inside the last 10 mm, or below 0) or backs up: the
     * opening waits for the end of the closing, then homes (bounded search) before opening. Moving up at once would
     * trust the count and drive the sleeve 25-35 mm past the top. The obstacle is removed with the On (homing
     * works, opens), or not (homing fails, gives up). */
    static const char *const phase_name[] = {"slowing onto the end stop (counted 0-5 mm)",
                                             "checking the end stop (counted below 0)", "backing up"};
    static bool (*const phase_cond[])(void) = {counted_in_slow_zone, counted_below_zero, backing_up};
    for (int phase = 0; phase < 3; phase++) {
        for (int removed = 1; removed >= 0; removed--) {
            if (!ready_at(TRAVEL / 2)) {
                return;
            }
            g.top = TRAVEL + 40 * STEPS_PER_MM; /* long screw: any overshoot shows */
            g.obstacle = 30 * STEPS_PER_MM;
            lift_move(0);
            CHECK(run_until(phase_cond[phase], move_ms(TRAVEL)));
            if (removed) {
                g.obstacle = 0;
            }
            lift_move(TRAVEL);
            CHECK(g.refused > 0 && s_last_target == 0); /* not applied yet */
            CHECK(run_until(closing_failed, move_ms(TRAVEL)));
            press_reset(); /* what the homing at the On presses, if it is still there */
            CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
            printf("  On while %s, obstacle %s: %d not closed, homing again 0x%x, position %ld, sleeve %.1f mm, "
                   "highest %.1f mm, pressed again %lld ms, %s\n",
                   phase_name[phase], removed ? "removed" : "still there", g.not_closed, g.rehome_err,
                   (long)stepper_position(), (double)g.phys / STEPS_PER_MM, (double)g.max_phys / STEPS_PER_MM,
                   (long long)press_ms(), s_stopped ? "gave up" : "running");
            CHECK(g.not_closed == 1 && g.rehomes == 1);
            CHECK(g.max_phys <= (removed ? TRAVEL : TRAVEL / 2)); /* never past full open, or where it started */
            CHECK(g.steps_blocked_top == 0);
            if (removed) {
                CHECK(g.rehome_err == ESP_OK && !s_stopped && stepper_position() == TRAVEL);
                CHECK(labs(g.phys - TRAVEL) <= TOUCH_DEPTH);
            } else {
                /* The bounded search (75 + 13 mm) presses on it again, then backs up 10 mm and stops for good */
                long rise = TRAVEL / 2 + mm_to_steps(RELEASE_MM); /* moved up since the last zero */
                CHECK(g.rehome_err == ESP_ERR_NOT_FOUND && s_stopped && g.follow_level);
                CHECK(press_ms() <= (rise + s_window - mm_to_steps(RELEASE_MM)) * 1000L / HOMING_SPS +
                                         1000L * HOMING_SPS / ACCEL_SPS2 + 500);
                CHECK(g.phys >= g.obstacle + mm_to_steps(RELEASE_MM) - 1);
                CHECK(CONFIG_MOTOR_IDLE_OFF_MS > 0 ? EN_OFF() : en_active());
                long steps = g.steps;
                g.obstacle = 0;
                lift_move(TRAVEL / 4); /* ignored until a restart */
                CHECK(settle(1000) >= 0 && g.steps == steps && g.follow_level);
            }
            check_signals();
        }
    }
}

static void scenario_press_time(void)
{
    /* README "An toàn": how long the motor presses on something in the way when switched Off (first to last step
     * lost on it), and again at the next command if it is still there (bounded homing search) */
    static const struct {
        int from_pct, at_mm;
    } cases[] = {{50, 30}, {100, 30}, {100, 75}, {100, CONFIG_MOTOR_TRAVEL_MM - 10}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        long from = TRAVEL * cases[i].from_pct / 100;
        long at = (long)cases[i].at_mm * STEPS_PER_MM;
        if (!ready_at(from)) {
            return;
        }
        g.obstacle = at;
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
        int64_t first = press_ms();
        /* counted from the obstacle down to the slow zone at the normal speed, then 15 mm at the homing speed */
        long slow = mm_to_steps(CLOSE_SLOW_MM), seek = mm_to_steps(CLOSE_SEEK_MM);
        int64_t bound = (at - slow) * 1000L / SPEED_SPS + (slow + seek) * 1000L / HOMING_SPS +
                        1000L * SPEED_SPS / ACCEL_SPS2 + 500;
        CHECK(g.not_closed == 1 && first > 0 && first <= bound);
        CHECK(g.max_phys <= from && g.phys >= at + mm_to_steps(RELEASE_MM) - 1);
        press_reset();
        lift_move(from); /* still there */
        CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
        int64_t again = press_ms();
        long rise = from + mm_to_steps(RELEASE_MM); /* moved up since the last zero */
        CHECK(s_stopped && g.rehome_err == ESP_ERR_NOT_FOUND && g.follow_level);
        CHECK(again <= (rise + s_window - mm_to_steps(RELEASE_MM)) * 1000L / HOMING_SPS +
                           1000L * HOMING_SPS / ACCEL_SPS2 + 500);
        CHECK(g.max_phys <= from && g.phys >= at + mm_to_steps(RELEASE_MM) - 1); /* backed up again */
        CHECK(CONFIG_MOTOR_IDLE_OFF_MS > 0 ? EN_OFF() : en_active());
        printf("  Off from %d %% (%.0f mm), obstacle at %d mm: pressed %.1f s (bound %.1f s), at the next command "
               "again %.1f s, then %s at %.1f mm, driver %s\n",
               cases[i].from_pct, (double)from / STEPS_PER_MM, cases[i].at_mm, first / 1000.0, bound / 1000.0,
               again / 1000.0, s_stopped ? "stopped" : "running", (double)g.phys / STEPS_PER_MM,
               EN_OFF() ? "off" : "on");
    }
}

static void scenario_held_at_top(void)
{
    /* Fully open, the sleeve cannot go down at all (held from below) when switched Off. The closing counts down to
     * -5 mm, then backs up 10 mm: 10 mm above full open. The homing at the next command (still held) backs up
     * 10 mm more, then stops for good: at most 20 mm above full open (README: mechanical stop above full open). */
    if (!ready_at(TRAVEL)) {
        return;
    }
    g.top = TRAVEL + 40 * STEPS_PER_MM;
    g.obstacle = TRAVEL;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    long after_close = g.max_phys;
    g.obstacle = g.phys; /* still held where it is */
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
    printf("  held at full open: highest %.1f mm after the closing, %.1f mm after homing again (0x%x), driver %s\n",
           (double)after_close / STEPS_PER_MM, (double)g.max_phys / STEPS_PER_MM, g.rehome_err,
           EN_OFF() ? "off" : "on");
    CHECK(g.not_closed == 1 && after_close <= TRAVEL + mm_to_steps(RELEASE_MM));
    CHECK(g.rehome_err == ESP_ERR_NOT_FOUND && s_stopped && g.follow_level &&
          g.max_phys <= TRAVEL + 2 * mm_to_steps(RELEASE_MM));
    /* A mechanical stop 12 mm above full open: the extra back-up stalls on it (steps lost), nothing worse */
    if (!ready_at(TRAVEL)) {
        return;
    }
    g.top = TRAVEL + 12 * STEPS_PER_MM;
    g.obstacle = TRAVEL;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    g.obstacle = g.phys;
    lift_move(TRAVEL / 2);
    CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
    printf("  same with a stop 12 mm above full open: %ld steps lost on it (%.1f mm)\n", g.steps_blocked_top,
           (double)g.steps_blocked_top / STEPS_PER_MM);
    CHECK(s_stopped && g.follow_level && g.max_phys == g.top);
    check_signals();
}

static bool touch_in_slow_zone(void)
{
    return s_close == CLOSE_TOUCH && stepper_position() <= mm_to_steps(CLOSE_SLOW_MM);
}

static bool opening_taken(void)
{
    return s_last_target == TRAVEL / 2;
}

static bool touch_above_slow_zone(void)
{
    return s_close == CLOSE_TOUCH && stepper_position() > mm_to_steps(CLOSE_SLOW_MM) + STEPS_PER_MM / 2;
}

static bool below_11mm(void)
{
    return stepper_position() <= mm_to_steps(11);
}

static void scenario_closing_smooth(void)
{
    /* Dimmed to about 1 LED (the sleeve heads down to 5 mm), then Off as it passes 11 mm: it keeps going down,
     * slowing to the homing speed, never turning back up to the start of the slow zone */
    if (!ready_at(TRAVEL)) {
        return;
    }
    lift_move(mm_to_steps(5));
    CHECK(run_until(below_11mm, move_ms(TRAVEL)));
    long up = g.steps_up;
    lift_move(0);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    printf("  Off at %.1f mm while going down: %ld steps up afterwards\n", (double)mm_to_steps(11) / STEPS_PER_MM,
           g.steps_up - up);
    CHECK(g.steps_up == up && stepper_position() == 0 && g.pressed && g.not_closed == 0);
    /* From full open: the last 10 mm slow down from the normal speed to the homing speed without stopping first */
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    g.closing_rest_ticks = 0;
    lift_move(0);
    int64_t ms = settle(move_ms(TRAVEL));
    printf("  closing from full open: %lld ms, %ld ticks at rest above 0 before the end stop\n", (long long)ms,
           g.closing_rest_ticks);
    CHECK(ms >= 0 && g.closing_rest_ticks == 0 && stepper_position() == 0 && g.pressed);
    /* The slow-down starts above the slow zone (by the braking distance): an On there is taken at once */
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
    lift_move(0);
    CHECK(run_until(touch_above_slow_zone, move_ms(TRAVEL)));
    int refused = g.refused;
    long at = stepper_position();
    CHECK(s_close == CLOSE_TOUCH && at > mm_to_steps(CLOSE_SLOW_MM));
    lift_move(TRAVEL / 2);
    printf("  On at %.1f mm while slowing down for the end stop: %s\n", (double)at / STEPS_PER_MM,
           s_last_target == TRAVEL / 2 ? "taken at once" : "refused");
    CHECK(s_last_target == TRAVEL / 2 && g.refused == refused);
    CHECK(settle(move_ms(TRAVEL)) >= 0 && stepper_position() == TRAVEL / 2 && labs(g.phys - TRAVEL / 2) <= TOUCH_DEPTH);
    /* An On just inside the last 10 mm waits for the end stop (README: about 1.3 s with the defaults) */
    lift_move(0);
    CHECK(run_until(touch_in_slow_zone, move_ms(TRAVEL)));
    int64_t t0 = g_sim_now_us;
    lift_move(TRAVEL / 2);
    CHECK(run_until(opening_taken, 10000));
    int64_t wait = (g_sim_now_us - t0) / 1000;
    printf("  On at %.1f mm (inside the last 10 mm): taken after %lld ms, once closed\n",
           (double)mm_to_steps(CLOSE_SLOW_MM) / STEPS_PER_MM, (long long)wait);
    CHECK(wait <= mm_to_steps(CLOSE_SLOW_MM) * 1000L / HOMING_SPS + 500 && g.rezero_reports == 0);
    CHECK(settle(move_ms(TRAVEL)) >= 0 && stepper_position() == TRAVEL / 2);
    check_signals();
}

static void scenario_lost_before_closing(void)
{
    /* Steps lost going down before the closing (README "Khi giảm độ sáng" and "Mở lại trong lúc ống che đang bị
     * chặn"): the count is below the sleeve when the closing starts. The closing misses the end stop; the homing at
     * the next command must still reach it once the obstacle is gone (its search covers every step moved up since the
     * last zero). */
    static const struct {
        const char *what;
        int from_pct, held_mm, to_pct; /* open to from_pct, something holds the sleeve at held_mm, go to to_pct */
        bool via_off;                  /* to_pct is an opening sent during a closing (Off first) */
    } cases[] = {
        {"dimmed to 10 % while held at 60 mm", 100, 60, 10, false},
        {"dimmed to 3 % while held at 40 mm", 50, 40, 3, false},
        {"Off, held at 140 mm, then 10 % while held", 100, 140, 10, true},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (!ready_at(TRAVEL * cases[i].from_pct / 100)) {
            return;
        }
        g.obstacle = (long)cases[i].held_mm * STEPS_PER_MM;
        if (cases[i].via_off) {
            lift_move(0);
            CHECK(run_until(closing_failed, 1500) == false); /* presses on it for a while */
        }
        lift_move(TRAVEL * cases[i].to_pct / 100);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        long count = stepper_position(), sleeve = g.phys;
        g.obstacle = 0; /* removed */
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        int not_closed = g.not_closed;
        lift_move(TRAVEL / 2);
        CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
        printf("  %s: count %.1f mm, sleeve %.1f mm; Off: %s; On: homing 0x%x, at %.1f mm, %s\n", cases[i].what,
               (double)count / STEPS_PER_MM, (double)sleeve / STEPS_PER_MM,
               not_closed ? "missed the end stop" : "closed",
               g.rehome_err, (double)g.phys / STEPS_PER_MM, s_stopped ? "gave up" : "running");
        CHECK(!s_stopped && stepper_position() == TRAVEL / 2 && labs(g.phys - TRAVEL / 2) <= TOUCH_DEPTH);
        CHECK(g.not_closed <= 1 && (g.not_closed == 0 || (g.rehomes >= 1 && g.rehome_err == ESP_OK)));
        CHECK(g.max_phys <= TRAVEL && g.steps_blocked_top == 0);
        check_signals();
    }
}

static bool touching(void)
{
    return s_close == CLOSE_TOUCH && stepper_position() > 0 && stepper_position() < mm_to_steps(CLOSE_SLOW_MM) / 2;
}

static void scenario_strikes_in_a_row(void)
{
    /* "3 failed closings in a row": a closing that reaches the end stop resets the count, also when an On waits
     * for it (sent while it slows onto the end stop) */
    if (!ready_at(TRAVEL / 2)) {
        return;
    }
    g.limit_us = g_sim_now_us + 3600LL * 1000 * 1000; /* 11 closings and homings: long at 3 mm/s */
    for (int round = 0; round < 4 && !s_stopped; round++) {
        /* a failed closing, then the obstacle is removed and On homes and opens */
        g.obstacle = 30 * STEPS_PER_MM;
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        g.obstacle = 0;
        lift_move(TRAVEL / 2);
        CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
        /* a good closing with an On waiting for it */
        lift_move(0);
        CHECK(run_until(touching, move_ms(TRAVEL)));
        lift_move(TRAVEL / 2);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        CHECK(s_not_closed == 0);
    }
    printf("  4 x (failed closing, good closing with On waiting): %d failed, count now %d, %s\n", g.not_closed,
           s_not_closed, s_stopped ? "gave up" : "running");
    CHECK(g.not_closed == 4 && !s_stopped && stepper_position() == TRAVEL / 2);
    /* three in a row: gives up */
    for (int i = 0; i < 3; i++) {
        g.obstacle = 30 * STEPS_PER_MM;
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        g.obstacle = 0;
        lift_move(TRAVEL / 2);
        CHECK(settle(move_ms(TRAVEL) * 4) >= 0);
    }
    CHECK(s_stopped && g.not_closed == 7 && g.follow_level);
    check_signals();
}

static void scenario_reversed_motor(void)
{
    /* First power on with the motor turning the wrong way (README "Chạy thử lần đầu" step 3): information only */
    boot(TRAVEL / 2);
    g.wired_reversed = true;
    esp_err_t err;
    int64_t ms = home(&err);
    printf("  motor reversed, half open: err 0x%x after %lld ms, sleeve went up to %.1f mm (%ld steps against the "
           "top)\n",
           err, (long long)ms, (double)g.max_phys / STEPS_PER_MM, g.steps_blocked_top);
    CHECK(err != ESP_OK);
    stepper_disable();

    boot(0);
    g.wired_reversed = true;
    ms = home(&err);
    printf("  motor reversed, closed: err 0x%x after %lld ms, %ld steps against the base\n", err, (long long)ms,
           g.steps_blocked_bottom);
    CHECK(err == ESP_ERR_INVALID_STATE);
    stepper_disable();
}

typedef void (*scenario_fn)(void);

static void run(const char *name, scenario_fn fn)
{
    int before = g_failures;
    printf("%s\n", name);
    if (setjmp(g_jmp) == 0) {
        g_jmp_armed = true;
        fn();
    }
    g_jmp_armed = false;
    if (g_failures != before) {
        printf("  -> %d failed check(s)\n", g_failures - before);
    }
}

#define RUN(fn) run(#fn, fn)

int main(void)
{
    printf("test_stepper: %ld steps/mm, travel %ld steps, homing %ld steps/s (slow %ld), NC %d, DIR_INVERT %d, "
           "EN GPIO %d, IDLE_OFF %d ms\n",
           (long)STEPS_PER_MM, (long)TRAVEL, (long)HOMING_SPS, (long)SLOW_SPS, CONFIG_MOTOR_ENDSTOP_NC,
           CONFIG_MOTOR_DIR_INVERT, CONFIG_MOTOR_EN_GPIO, CONFIG_MOTOR_IDLE_OFF_MS);
    RUN(scenario_home_half_open);
    RUN(scenario_home_full_open);
    RUN(scenario_home_closed);
    RUN(scenario_home_chatter);
    RUN(scenario_broken_wire_at_boot);
    RUN(scenario_motor_missing);
    RUN(scenario_moves);
    RUN(scenario_rest);
    RUN(scenario_lost_steps_up);
    RUN(scenario_lost_steps_closing);
    RUN(scenario_obstacle_closing);
    RUN(scenario_wire_breaks_after_homing);
    RUN(scenario_wire_breaks_closed);
    RUN(scenario_endstop_glitch_after_homing);
    RUN(scenario_open_while_closing_fails);
    RUN(scenario_press_time);
    RUN(scenario_held_at_top);
    RUN(scenario_closing_smooth);
    RUN(scenario_lost_before_closing);
    RUN(scenario_strikes_in_a_row);
    RUN(scenario_reversed_motor);
    return check_report();
}
