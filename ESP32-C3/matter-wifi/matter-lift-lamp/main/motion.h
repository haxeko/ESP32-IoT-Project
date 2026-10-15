#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Trapezoidal motion planner for a step motor, run from a fixed-rate timer interrupt (one call per tick).
 * Pure integer code, no division in motion_tick(): safe in an IRAM interrupt handler, and testable on a computer.
 *
 * Speed is in steps per tick as a Q32 fraction (2^32 = one step every tick); a phase accumulator turns it into
 * steps. The speed is capped at half the tick rate, so a step pulse always has one tick high and one tick low.
 * The target can change at any time, even while moving: the planner brakes in time to stop exactly on it, and
 * when the new target is behind, it brakes first and comes back (it never stops abruptly, except within the
 * last step).
 */
typedef struct {
    /* Limits (Q32 steps per tick, Q32 steps per tick per tick) */
    uint32_t vmax;
    uint32_t accel;
    /* State: the interrupt handler owns it; tasks only write target (one aligned 32-bit store) */
    volatile int32_t target;
    volatile int32_t pos;
    volatile int8_t dir; /* direction of the current motion: +1, -1, or 0 at rest */
    uint32_t vel;
    uint32_t phase;
} motion_t;

/** Speed in steps per second -> Q32 steps per tick (clamped to half the tick rate). */
uint32_t motion_speed_q32(uint32_t steps_per_s, uint32_t tick_hz);

/** Acceleration in steps per second^2 -> Q32 steps per tick^2 (at least 1). */
uint32_t motion_accel_q32(uint32_t steps_per_s2, uint32_t tick_hz);

/** At rest at position 0, target 0. */
void motion_init(motion_t *m, uint32_t vmax_q32, uint32_t accel_q32);

/**
 * Advance one tick. Returns +1 or -1 when a step must be output this tick (pos already updated), else 0.
 * The direction of a step never changes while moving: a step is only output in m->dir.
 */
int motion_tick(motion_t *m);

/** At rest on the target. */
bool motion_idle(const motion_t *m);

/**
 * Stop at once and declare the current position (end stop reached). The target is left alone: the caller decides
 * where to go next, and must not let motion_tick() step towards a target that is blocked by the end stop.
 */
void motion_halt(motion_t *m, int32_t pos);

#ifdef __cplusplus
}
#endif
