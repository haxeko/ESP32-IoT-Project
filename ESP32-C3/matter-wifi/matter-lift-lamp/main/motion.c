#include "motion.h"

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define MOTION_IRAM IRAM_ATTR /* called from a cache-safe timer interrupt */
#else
#define MOTION_IRAM
#endif

#define VEL_LIMIT (1u << 31) /* one step every 2 ticks: STEP high one tick, low the next */

uint32_t motion_speed_q32(uint32_t steps_per_s, uint32_t tick_hz)
{
    uint64_t v = ((uint64_t)steps_per_s << 32) / tick_hz;
    return v > VEL_LIMIT ? VEL_LIMIT : (uint32_t)v;
}

uint32_t motion_accel_q32(uint32_t steps_per_s2, uint32_t tick_hz)
{
    uint64_t a = ((uint64_t)steps_per_s2 << 32) / ((uint64_t)tick_hz * tick_hz);
    return a == 0 ? 1 : a > VEL_LIMIT ? VEL_LIMIT : (uint32_t)a;
}

void motion_init(motion_t *m, uint32_t vmax_q32, uint32_t accel_q32)
{
    m->vmax = vmax_q32 > VEL_LIMIT ? VEL_LIMIT : vmax_q32;
    m->accel = accel_q32 ? accel_q32 : 1;
    m->target = 0;
    m->pos = 0;
    m->dir = 0;
    m->vel = 0;
    m->phase = 0;
}

int MOTION_IRAM motion_tick(motion_t *m)
{
    int32_t target = m->target; /* read once: a task may change it at any time */
    int32_t pos = m->pos;
    int32_t remaining = target - pos;

    if (m->vel == 0) {
        if (remaining == 0) {
            m->dir = 0;
            return 0;
        }
        if (m->dir == 0 || (remaining > 0) != (m->dir > 0)) {
            /* Start, or turn around after braking to a stop */
            m->dir = remaining > 0 ? 1 : -1;
            m->phase = 0;
        }
    }

    /* Steps still available in the current direction (0 on the target, or when it is behind) */
    uint32_t left = 0;
    if (remaining != 0 && (remaining > 0) == (m->dir > 0)) {
        left = remaining > 0 ? (uint32_t)remaining : (uint32_t)(-(int64_t)remaining);
    }

    uint64_t v2 = ((uint64_t)m->vel * m->vel) >> 32;
    if (left == 0 && v2 < (uint64_t)4 * m->accel) {
        /* On (or just past) the target and slow enough to stop within about 2 steps: stop here, and turn around on
         * a later tick if the target is behind. A target set closer than the braking distance (or reached too fast)
         * is passed while braking, then approached again: never an abrupt stop from speed. */
        m->vel = 0;
        m->phase = 0;
        if (remaining == 0) {
            m->dir = 0;
        }
        return 0;
    }

    /* Braking distance v^2 / 2a, compared without division: brake when vel^2 / 2^32 >= 2 * accel * left */
    if (left == 0 || v2 >= (uint64_t)2 * m->accel * left || m->vel > m->vmax) {
        m->vel = m->vel > m->accel ? m->vel - m->accel : 0;
    } else if (m->vel < m->vmax) {
        uint32_t room = m->vmax - m->vel;
        m->vel += m->accel < room ? m->accel : room;
    }

    uint32_t before = m->phase;
    m->phase += m->vel;
    if (m->phase < before) { /* wrapped: one step (at most one per tick, vel <= 2^31) */
        m->pos = pos + m->dir;
        return m->dir;
    }
    return 0;
}

bool motion_idle(const motion_t *m)
{
    return m->vel == 0 && m->pos == m->target;
}

void MOTION_IRAM motion_halt(motion_t *m, int32_t pos)
{
    m->vel = 0;
    m->phase = 0;
    m->dir = 0;
    m->pos = pos;
}
