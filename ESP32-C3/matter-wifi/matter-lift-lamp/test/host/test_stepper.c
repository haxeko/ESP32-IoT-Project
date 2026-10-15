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
// (found by the end stop: fault, homing again), lost steps or an obstacle while closing (backs up, lets go, homes
// at the next command), end stop wire breaking (open or closed lamp) or a glitch after homing, contact chatter,
// motor turning the wrong way (information only). lift_react()/lift_move() do what main/lift.c does.
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
    long steps, steps_disabled, steps_blocked_bottom, steps_blocked_top, steps_slipped, steps_wrong_dir;
    long steps_outside_isr, moving_while_disabled_ticks;
    long min_phys, max_phys;
    int64_t last_rise_us, last_fall_us, last_dir_us, en_on_us;
    int64_t min_gap_us, last_gap_us, min_high_us, min_low_us, min_dir_setup_us;
    int64_t min_first_step_us; /* EN on -> first STEP */
    bool first_step_pending;
    int rezero_reports, faults, not_closed;
    esp_err_t rehome_err;
    int rehomes;
    bool gave_up;      /* lift.c give_up(): motor off until restart */
    long target;       /* last target the "lift task" asked for */
    bool has_target;
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
        g.steps_blocked_obstacle++;
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

/* What main/lift.c does on a stepper event */
static void lift_react(stepper_event_t ev)
{
    switch (ev) {
    case STEPPER_EVENT_REZEROED:
        g.rezero_reports++;
        break;
    case STEPPER_EVENT_FAULT: /* home again at once, give up if that fails, then back to the wanted height */
        g.faults++;
        g.rehomes++;
        g.rehome_err = stepper_home();
        if (g.rehome_err != ESP_OK) {
            stepper_disable();
            g.gave_up = true;
        } else if (g.has_target) {
            stepper_move_to(g.target);
        }
        break;
    case STEPPER_EVENT_NOT_CLOSED: /* home again at the next command; the 3rd time, give up (MAX_NOT_CLOSED) */
        if (++g.not_closed >= 3) {
            stepper_disable();
            g.gave_up = true;
        }
        break;
    case STEPPER_EVENT_NONE:
        break;
    }
}

/* What main/lift.c does on a new target: home first if the stepper is not ready (after a failed closing) */
static void lift_move(long target)
{
    g.target = target;
    g.has_target = true;
    if (g.gave_up) {
        return;
    }
    if (!stepper_ready()) {
        g.rehomes++;
        g.rehome_err = stepper_home();
        if (g.rehome_err != ESP_OK) {
            stepper_disable();
            g.gave_up = true;
            return;
        }
    }
    stepper_move_to(target);
}

/* The lift task (lift.c) after a new target: poll every 20 ms while moving, 250 ms at rest. Returns the time (ms)
 * until the motor is at rest with the timer interrupt stopped, -1 if not within max_ms. */
static int64_t settle(int64_t max_ms)
{
    int64_t t0 = g_sim_now_us;
    for (;;) {
        lift_react(stepper_poll());
        if (!stepper_busy() && g_sim_gptimer.fsm != SIM_GPTIMER_RUN) {
            return (g_sim_now_us - t0) / 1000;
        }
        if (g_sim_now_us - t0 > max_ms * 1000) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(stepper_busy() ? 20 : 250));
    }
}

static void wait_ms(int64_t ms) /* lift task at rest: poll every 250 ms */
{
    int64_t end = g_sim_now_us + ms * 1000;
    while (g_sim_now_us < end) {
        lift_react(stepper_poll());
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

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
    /* the whole search (125 % of the travel) at the homing speed, then it gives up */
    CHECK(ms <= (TRAVEL + TRAVEL / 4) * 1000L / HOMING_SPS + 2000L * HOMING_SPS / ACCEL_SPS2 + 3000);
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
            lift_react(stepper_poll());
            vTaskDelay(pdMS_TO_TICKS(stepper_busy() ? 20 : 250));
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
    CHECK(g.faults == 1 && g.rehome_err == ESP_OK && !g.gave_up);
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
    /* 6.5 mm lost on the way down, more than the 5 mm searched below 0: backs up 10 mm and lets go */
    CHECK(g.not_closed == 1 && !stepper_ready() && !stepper_busy());
    CHECK(EN_OFF() || CONFIG_MOTOR_EN_GPIO < 0);
    CHECK(g_sim_gptimer.fsm == SIM_GPTIMER_ENABLE);
    /* The next command homes first, then opens: never past full open */
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
    long pushed = g.steps_blocked_obstacle;
    printf("  obstacle at 30 mm: %d not closed after %lld ms, %ld steps pushed against it (%.1f s at %ld steps/s "
           "max), sleeve backed to %.1f mm, driver %s\n",
           g.not_closed, (long long)(g_sim_now_us - t0) / 1000, pushed, (double)pushed / SPEED_SPS, (long)SPEED_SPS,
           (double)g.phys / STEPS_PER_MM, EN_OFF() ? "off" : "on");
    CHECK(g.not_closed == 1 && (EN_OFF() || CONFIG_MOTOR_EN_GPIO < 0));
    CHECK(g.phys >= g.obstacle + mm_to_steps(RELEASE_MM) - 1); /* backed away from it */
    CHECK(g.max_phys <= TRAVEL);
    /* Removed; the next command homes and works */
    g.obstacle = 0;
    lift_move(TRAVEL / 4);
    CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    CHECK(g.rehome_err == ESP_OK && labs(g.phys - TRAVEL / 4) <= TOUCH_DEPTH && !g.gave_up);
    /* Blocked three times: gives up (motor off) until a restart */
    for (int i = 0; i < 2 && !g.gave_up; i++) {
        g.obstacle = 20 * STEPS_PER_MM;
        lift_move(0);
        CHECK(settle(move_ms(TRAVEL) * 2) >= 0);
        g.obstacle = 0;
        lift_move(TRAVEL / 4);
        CHECK(settle(move_ms(TRAVEL) * 3) >= 0);
    }
    printf("  %d failed closings: %s\n", g.not_closed, g.gave_up ? "gave up, motor off" : "still trying");
    CHECK(g.not_closed == 3 && g.gave_up && (EN_OFF() || CONFIG_MOTOR_EN_GPIO < 0));
    long steps = g.steps;
    lift_move(TRAVEL / 2); /* ignored until a restart */
    CHECK(settle(1000) >= 0 && g.steps == steps);
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
           (long)stepper_position(), (double)g.phys / STEPS_PER_MM, g.faults, g.rehome_err, g.gave_up ? "yes" : "no");
    lift_move(TRAVEL);
    CHECK(settle(move_ms(TRAVEL)) >= 0);
#if CONFIG_MOTOR_ENDSTOP_NC
    /* NC: the broken wire reads pressed far above 0: fault, homing again finds it stuck, motor off */
    CHECK(g.faults == 1 && g.rehome_err == ESP_ERR_INVALID_STATE && g.gave_up);
    CHECK(EN_OFF() || CONFIG_MOTOR_EN_GPIO < 0);
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
    CHECK(g.faults == 1 && g.rehome_err == ESP_ERR_INVALID_STATE && g.gave_up);
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
    RUN(scenario_reversed_motor);
    return check_report();
}
