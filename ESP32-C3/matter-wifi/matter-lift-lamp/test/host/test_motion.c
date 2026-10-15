// Host test of the step motor planner (main/motion.c) and of the height <-> LEDs mapping (main/lift_map.h):
// exact arrival, speed and acceleration limits, no step faster than every 2 ticks, target changes while moving
// (further, back, many small ones), halt.
#include "check.h"

#include <math.h>
#include <stdlib.h>

#include "lift_map.h"
#include "motion.h"

#define TICK_HZ 20000
#define STEPS_PER_MM 200
#define VMAX_SPS (10 * STEPS_PER_MM)   /* 10 mm/s */
#define ACCEL_SPS2 (20 * STEPS_PER_MM) /* 20 mm/s^2 */

/* One simulated move: ticks until at rest, with the measured extremes */
struct run {
    long ticks;
    long steps;
    int dir_changes;
    long min_gap;      /* ticks between two steps */
    double max_speed;  /* steps/s, from the gap between steps */
    long overshoot;    /* furthest beyond the final target, in the direction of travel */
    long jerks;        /* ticks where the speed changed by more than one acceleration increment */
    double arrive_speed; /* fastest speed (steps/s) dropped to 0 on arriving at a target */
    double end_speed;  /* speed of the last step */
};

static struct run run_until_idle(motion_t *m, long max_ticks, void (*hook)(motion_t *, long tick))
{
    struct run r = {.min_gap = 1L << 30};
    long last_step = -1;
    int last_dir = 0;
    for (long t = 0; t < max_ticks; t++) {
        if (hook) {
            hook(m, t);
        }
        uint32_t vel_before = m->vel;
        int s = motion_tick(m);
        uint32_t dv = m->vel > vel_before ? m->vel - vel_before : vel_before - m->vel;
        if (m->vel == 0 && vel_before != 0) {
            /* A stop (on the target, or to turn around): only from the speed of the last couple of steps */
            double v = (double)vel_before / 4294967296.0 * TICK_HZ;
            if (v > r.arrive_speed) {
                r.arrive_speed = v;
            }
            if ((((uint64_t)vel_before * vel_before) >> 32) >= (uint64_t)4 * m->accel) {
                r.jerks++;
            }
        } else if (dv > m->accel) {
            r.jerks++;
        }
        if (s) {
            r.steps++;
            if (last_dir && s != last_dir) {
                r.dir_changes++;
            }
            last_dir = s;
            if (last_step >= 0) {
                long gap = t - last_step;
                if (gap < r.min_gap) {
                    r.min_gap = gap;
                }
                double speed = (double)TICK_HZ / gap;
                if (speed > r.max_speed) {
                    r.max_speed = speed;
                }
                r.end_speed = speed;
            }
            last_step = t;
            long beyond = (long)(m->pos - m->target) * s;
            if (beyond > r.overshoot) {
                r.overshoot = beyond;
            }
        }
        if (motion_idle(m) && (!hook || t > 4L * TICK_HZ)) { /* hooks change targets during the first 4 s */
            r.ticks = t + 1;
            return r;
        }
    }
    r.ticks = -1; /* did not finish */
    return r;
}

static motion_t new_motion(void)
{
    motion_t m;
    motion_init(&m, motion_speed_q32(VMAX_SPS, TICK_HZ), motion_accel_q32(ACCEL_SPS2, TICK_HZ));
    return m;
}

static void test_conversions(void)
{
    CHECK(motion_speed_q32(TICK_HZ / 2, TICK_HZ) == (1u << 31));
    CHECK(motion_speed_q32(TICK_HZ * 3, TICK_HZ) == (1u << 31)); /* clamped: one step every 2 ticks */
    CHECK(motion_speed_q32(0, TICK_HZ) == 0);
    CHECK(motion_accel_q32(0, TICK_HZ) == 1);
    uint32_t v = motion_speed_q32(2000, TICK_HZ);
    CHECK(v > 429496729u - 2 && v < 429496729u + 2); /* 0.1 step/tick */
}

static void test_full_travel(void)
{
    motion_t m = new_motion();
    m.target = 150 * STEPS_PER_MM;
    struct run r = run_until_idle(&m, 30L * TICK_HZ, NULL);
    double secs = (double)r.ticks / TICK_HZ;
    /* 150 mm at 10 mm/s with 0.5 s ramps: 15.5 s */
    printf("  full travel: %.2f s, max %.0f steps/s, arrival %.0f steps/s, min gap %ld ticks\n", secs, r.max_speed,
           r.arrive_speed, r.min_gap);
    CHECK(r.ticks > 0 && m.pos == 150 * STEPS_PER_MM && r.steps == 150 * STEPS_PER_MM);
    CHECK(secs > 15.3 && secs < 15.8);
    CHECK(r.dir_changes == 0 && r.overshoot == 0);
    CHECK(r.min_gap >= 2);
    CHECK(r.max_speed <= VMAX_SPS * 1.02);
    CHECK(r.arrive_speed < 200 && r.jerks == 0); /* braked all the way: stops from about sqrt(2 a) */
    CHECK(motion_idle(&m) && m.dir == 0);

    /* and back */
    m.target = 0;
    r = run_until_idle(&m, 30L * TICK_HZ, NULL);
    CHECK(r.ticks > 0 && m.pos == 0 && r.steps == 150 * STEPS_PER_MM && r.overshoot == 0 && r.dir_changes == 0);
}

static void test_short_moves(void)
{
    static const int dist[] = {1, 2, 3, 10, 57, 100, 999, -1, -2, -5, -300};
    for (unsigned i = 0; i < sizeof(dist) / sizeof(dist[0]); i++) {
        motion_t m = new_motion();
        m.pos = 5000;
        m.target = 5000 + dist[i];
        struct run r = run_until_idle(&m, 10L * TICK_HZ, NULL);
        CHECK(r.ticks > 0 && m.pos == 5000 + dist[i] && r.steps == abs(dist[i]));
        CHECK(r.overshoot == 0 && r.dir_changes == 0 && r.jerks == 0 && r.arrive_speed < 200);
        if (r.ticks <= 0 || m.pos != 5000 + dist[i]) {
            printf("  short move %d: ticks %ld pos %ld\n", dist[i], r.ticks, (long)m.pos);
        }
    }
}

/* Target changes during the motion */
static long s_change_at;
static int32_t s_new_target;
static void change_once(motion_t *m, long t)
{
    if (t == s_change_at) {
        m->target = s_new_target;
    }
}

static void test_reverse_while_moving(void)
{
    motion_t m = new_motion();
    m.target = 20000;
    s_change_at = 3 * TICK_HZ; /* at full speed */
    s_new_target = 1000;
    struct run r = run_until_idle(&m, 60L * TICK_HZ, change_once);
    /* braking distance from 2000 steps/s at 4000 steps/s^2: v^2/2a = 500 steps */
    CHECK(m.pos == 1000 && r.dir_changes == 1);
    CHECK(r.jerks == 0 && r.arrive_speed < 200); /* brakes, turns around: no abrupt stop */
    CHECK(r.min_gap >= 2);
}

static void test_extend_while_braking(void)
{
    motion_t m = new_motion();
    m.target = 3000;
    s_change_at = 1 * TICK_HZ + TICK_HZ / 2; /* while braking towards 3000 */
    s_new_target = 9000;
    struct run r = run_until_idle(&m, 60L * TICK_HZ, change_once);
    CHECK(m.pos == 9000 && r.dir_changes == 0 && r.overshoot == 0 && r.jerks == 0);
}

/* Matter transition: the level, so the target, moves up in small steps every 100 ms */
static void ramp_targets(motion_t *m, long t)
{
    if (t % (TICK_HZ / 10) == 0 && t / (TICK_HZ / 10) <= 30) {
        m->target = (int32_t)(t / (TICK_HZ / 10)) * 1000;
    }
}

/* Cruising at full speed, the target is moved to just ahead (closer than the braking distance, 500 steps) */
static void test_target_just_ahead(void)
{
    static const int ahead[] = {0, 1, 50, 200, 400, 499, 500, 600};
    for (unsigned i = 0; i < sizeof(ahead) / sizeof(ahead[0]); i++) {
        motion_t m = new_motion();
        m.target = 30000;
        for (int t = 0; t < 2 * TICK_HZ; t++) { /* 1 s of ramp, then cruising */
            motion_tick(&m);
        }
        int32_t at = m.pos;
        m.target = at + ahead[i];
        struct run r = run_until_idle(&m, 10L * TICK_HZ, NULL);
        long past = r.overshoot;
        CHECK(r.ticks > 0 && m.pos == at + ahead[i]);
        CHECK(r.jerks == 0 && r.arrive_speed < 200 && r.min_gap >= 2);
        CHECK(past <= 520); /* never further than the braking distance */
        if (r.jerks || r.arrive_speed >= 200 || m.pos != at + ahead[i]) {
            printf("  target %d ahead at full speed: pos %ld, stop from %.0f steps/s, %ld jerk(s), %ld past\n",
                   ahead[i], (long)(m.pos - at), r.arrive_speed, r.jerks, past);
        }
    }
}

/* Matter MoveToLevel with a transition while the lamp is opening: the target comes down by small steps */
static void ramp_down_targets(motion_t *m, long t)
{
    if (t == 0) {
        m->target = 30000;
    } else if (t >= TICK_HZ * 3 / 2 && t % (TICK_HZ / 10) == 0 && t <= TICK_HZ * 7 / 2) {
        int32_t step = (int32_t)((t - TICK_HZ * 3 / 2) / (TICK_HZ / 10)); /* 0..20 */
        m->target = 27000 - step * 1200;
    }
}

static void test_transition_down_while_opening(void)
{
    motion_t m = new_motion();
    struct run r = run_until_idle(&m, 60L * TICK_HZ, ramp_down_targets);
    CHECK(m.pos == 27000 - 20 * 1200 && r.jerks == 0 && r.arrive_speed < 200 && r.min_gap >= 2);
}

static void test_ramp(void)
{
    motion_t m = new_motion();
    struct run r = run_until_idle(&m, 120L * TICK_HZ, ramp_targets);
    CHECK(m.pos == 30000 && r.dir_changes == 0 && r.overshoot == 0 && r.min_gap >= 2 && r.jerks == 0);
}

static void test_halt(void)
{
    motion_t m = new_motion();
    m.target = 10000;
    for (int t = 0; t < TICK_HZ; t++) {
        motion_tick(&m);
    }
    CHECK(m.pos > 0 && m.dir == 1);
    motion_halt(&m, 0);
    CHECK(m.pos == 0 && m.dir == 0 && !motion_idle(&m)); /* the target is left alone */
    m.target = 0;
    CHECK(motion_idle(&m));
}

static void test_lift_map(void)
{
    static const struct {
        uint16_t leds;
        int32_t travel;
    } cases[] = {{30, 30000}, {30, 7500}, {7, 1000}, {144, 12345}, {1, 50}, {60, 60}};
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        uint16_t n = cases[c].leds;
        int32_t travel = cases[c].travel;
        CHECK(lift_steps_for_leds(0, n, travel) == 0 && lift_steps_for_leds(n, n, travel) == travel);
        CHECK(lift_steps_for_leds(n + 5, n, travel) == travel);
        int32_t prev = -1;
        for (uint16_t k = 0; k <= n; k++) {
            int32_t s = lift_steps_for_leds(k, n, travel);
            CHECK(s > prev);
            CHECK(lift_exposed_leds(s, n, travel) == k);
            prev = s;
        }
        uint16_t prev_lit = 0;
        for (int32_t p = -10; p <= travel + 10; p++) {
            uint16_t lit = lift_exposed_leds(p, n, travel);
            CHECK(lit >= prev_lit && lit <= prev_lit + 1 && lit <= n);
            prev_lit = lit;
        }
    }
}

int main(void)
{
    printf("motion:\n");
    test_conversions();
    test_full_travel();
    test_short_moves();
    test_reverse_while_moving();
    test_target_just_ahead();
    test_transition_down_while_opening();
    test_extend_while_braking();
    test_ramp();
    test_halt();
    test_lift_map();
    return check_report();
}
