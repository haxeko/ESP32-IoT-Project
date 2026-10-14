#include "ir_decode.h"

/* NEC timing, microseconds */
#define NEC_LEAD_MARK    9000
#define NEC_LEAD_SPACE   4500
#define NEC_REPEAT_SPACE 2250
#define NEC_BIT_MARK     560
#define NEC_BITS         32
/* Bit spaces: nominal 560 us for 0 and 1690 us for 1. IR receiver modules stretch the marks and shorten the
 * spaces by up to ~150 us, so classify by range rather than by a tight window. */
#define NEC_SPACE_MIN    200
#define NEC_SPACE_ONE    1100
#define NEC_SPACE_MAX    2400

static uint16_t add_saturated(uint16_t a, uint32_t b)
{
    const uint32_t sum = a + b;
    return sum > UINT16_MAX ? UINT16_MAX : (uint16_t)sum;
}

size_t ir_pulses_from_rmt(const uint32_t *symbols, size_t count, ir_pulse_t *pulses, size_t max_pulses)
{
    size_t n = 0;         /* complete pulses */
    bool in_mark = false; /* pulses[n] has its mark, not yet its space */
    for (size_t i = 0; i < count; i++) {
        for (int half = 0; half < 2; half++) {
            const uint32_t segment = symbols[i] >> (16 * half);
            const uint32_t duration = segment & 0x7FFF;
            const bool space = (segment >> 15) & 1;
            if (duration == 0) {
                /* End marker: the line went idle */
                return in_mark ? n + 1 : n;
            }
            if (!space) {
                if (in_mark) {
                    pulses[n].mark_us = add_saturated(pulses[n].mark_us, duration);
                } else if (n < max_pulses) {
                    pulses[n].mark_us = (uint16_t)duration;
                    pulses[n].space_us = 0;
                    in_mark = true;
                } else {
                    return n; /* buffer full */
                }
            } else if (in_mark) {
                pulses[n].space_us = (uint16_t)duration;
                n++;
                in_mark = false;
            } else if (n > 0) {
                pulses[n - 1].space_us = add_saturated(pulses[n - 1].space_us, duration);
            }
            /* else: idle level before the first mark */
        }
    }
    return in_mark ? n + 1 : n;
}

static bool near(uint32_t value, uint32_t nominal, uint32_t tolerance_pct)
{
    return value * 100 >= nominal * (100 - tolerance_pct) && value * 100 <= nominal * (100 + tolerance_pct);
}

static bool nec_decode(const ir_pulse_t *p, size_t count, ir_code_t *code)
{
    /* Leader, 32 data bits, stop mark */
    if (count < NEC_BITS + 2 || !near(p[0].mark_us, NEC_LEAD_MARK, 25) || !near(p[0].space_us, NEC_LEAD_SPACE, 25)) {
        return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < NEC_BITS; i++) {
        const ir_pulse_t *bit = &p[1 + i];
        if (!near(bit->mark_us, NEC_BIT_MARK, 50)) {
            return false;
        }
        if (bit->space_us >= NEC_SPACE_ONE && bit->space_us <= NEC_SPACE_MAX) {
            value |= 1UL << i;
        } else if (bit->space_us < NEC_SPACE_MIN || bit->space_us >= NEC_SPACE_ONE) {
            return false;
        }
    }
    if (!near(p[1 + NEC_BITS].mark_us, NEC_BIT_MARK, 50)) {
        return false;
    }
    code->proto = IR_PROTO_NEC;
    code->pulses = (uint8_t)(count > 255 ? 255 : count);
    code->reserved = 0;
    code->value = value;
    return true;
}

static bool nec_is_repeat(const ir_pulse_t *p, size_t count)
{
    /* Leader with a short space, then the stop mark */
    return count >= 2 && count <= 3 && near(p[0].mark_us, NEC_LEAD_MARK, 25) &&
           near(p[0].space_us, NEC_REPEAT_SPACE, 25) && near(p[1].mark_us, NEC_BIT_MARK, 50);
}

/* Duration k of the frame: mark 0, space 0, mark 1, space 1, ... */
static uint32_t duration_at(const ir_pulse_t *p, size_t k)
{
    return (k & 1) ? p[k >> 1].space_us : p[k >> 1].mark_us;
}

/* 0: clearly shorter, 2: clearly longer, 1: about the same (within 20 %) */
static uint32_t shape_compare(uint32_t before, uint32_t after)
{
    if (after * 10 < before * 8) {
        return 0;
    }
    if (before * 10 < after * 8) {
        return 2;
    }
    return 1;
}

static void shape_decode(const ir_pulse_t *p, size_t count, ir_code_t *code)
{
    size_t durations = count * 2;
    if (p[count - 1].space_us == 0) {
        durations--; /* the idle line ended the frame: not a real space */
    }
    /* FNV-1 over the comparison of each duration with the next one of the same kind (mark/mark, space/space) */
    uint32_t hash = 2166136261UL;
    for (size_t k = 0; k + 2 < durations; k++) {
        hash = (hash * 16777619UL) ^ shape_compare(duration_at(p, k), duration_at(p, k + 2));
    }
    code->proto = IR_PROTO_SHAPE;
    code->pulses = (uint8_t)(count > 255 ? 255 : count);
    code->reserved = 0;
    code->value = hash;
}

ir_frame_t ir_decode(const ir_pulse_t *pulses, size_t count, ir_code_t *code)
{
    if (pulses == NULL || count == 0) {
        return IR_FRAME_NONE;
    }
    if (nec_is_repeat(pulses, count)) {
        return IR_FRAME_REPEAT;
    }
    if (nec_decode(pulses, count, code)) {
        return IR_FRAME_CODE;
    }
    if (count < IR_SHAPE_MIN_PULSES) {
        return IR_FRAME_NONE;
    }
    shape_decode(pulses, count, code);
    return IR_FRAME_CODE;
}

bool ir_code_equal(const ir_code_t *a, const ir_code_t *b)
{
    return a->proto != IR_PROTO_NONE && a->proto == b->proto && a->value == b->value;
}

bool ir_press_filter_accept(ir_press_filter_t *filter, ir_frame_t frame, const ir_code_t *code, uint32_t now_ms)
{
    switch (frame) {
    case IR_FRAME_REPEAT:
        if (filter->active && now_ms - filter->last_ms < IR_PRESS_GAP_MS) {
            filter->last_ms = now_ms;
        }
        return false;
    case IR_FRAME_CODE:
        if (filter->active && ir_code_equal(code, &filter->last) && now_ms - filter->last_ms < IR_PRESS_GAP_MS) {
            filter->last_ms = now_ms;
            return false;
        }
        filter->last = *code;
        filter->last_ms = now_ms;
        filter->active = true;
        return true;
    default:
        return false;
    }
}
