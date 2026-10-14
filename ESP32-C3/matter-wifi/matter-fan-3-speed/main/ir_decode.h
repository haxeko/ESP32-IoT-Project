#pragma once

/* IR remote frame decoding. Pure C, no ESP-IDF dependency (tested on the host by test/host/run.sh).
 *
 * A frame is the list of (mark, space) pulses captured by the receiver until the line stays idle.
 * NEC frames (the protocol of most cheap remotes) are decoded to their 32 data bits. Any other protocol
 * is identified by the shape of the frame: each duration is compared with the duration two steps later
 * (shorter / about equal / longer) and the results are hashed. The shape does not depend on the exact
 * timing, so the same button always gives the same code, whatever the protocol. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One received pulse: carrier on (mark) then off (space), in microseconds. The space of the last pulse is
 *  0 (the receiver stopped on the idle line). */
typedef struct {
    uint16_t mark_us;
    uint16_t space_us;
} ir_pulse_t;

typedef enum {
    IR_PROTO_NONE = 0,  /**< empty slot (nothing learned) */
    IR_PROTO_NEC = 1,   /**< value = 32 NEC data bits, first received bit in bit 0 */
    IR_PROTO_SHAPE = 2, /**< value = hash of the frame shape */
} ir_proto_t;

/** A button code. Stored as is in NVS (ir_table_t): keep the layout. */
typedef struct {
    uint8_t proto;     /**< ir_proto_t */
    uint8_t pulses;    /**< number of pulses of the frame (for logs) */
    uint16_t reserved; /**< 0 */
    uint32_t value;
} ir_code_t;

typedef enum {
    IR_FRAME_NONE = 0, /**< noise, too short or unknown */
    IR_FRAME_CODE,     /**< a button code */
    IR_FRAME_REPEAT,   /**< NEC repeat frame: the button of the previous code is still held */
} ir_frame_t;

/** Frames with fewer pulses are ignored unless they are NEC (noise, e.g. from lamps, gives short bursts) */
#define IR_SHAPE_MIN_PULSES 8

/** Frames closer than this belong to the same button press: a held button repeats its frame every 40-120 ms, many
 *  remotes send each press 2-3 times, some send two different frames per press */
#define IR_PRESS_GAP_MS 220

/** Convert RMT RX symbols to pulses. Each symbol is rmt_symbol_word_t.val: bits 0-14 duration0, bit 15 level0,
 *  bits 16-30 duration1, bit 31 level1, durations in microseconds (1 MHz channel resolution). The receiver output
 *  is low while it sees the carrier: level 0 = mark, level 1 = space. Segments of the same level are merged, the
 *  idle level before the first mark is dropped and a zero duration ends the frame. Returns the number of pulses
 *  (at most max_pulses; a longer frame is truncated). */
size_t ir_pulses_from_rmt(const uint32_t *symbols, size_t count, ir_pulse_t *pulses, size_t max_pulses);

/** Decode a received frame. Returns IR_FRAME_CODE and fills *code, IR_FRAME_REPEAT, or IR_FRAME_NONE. */
ir_frame_t ir_decode(const ir_pulse_t *pulses, size_t count, ir_code_t *code);

bool ir_code_equal(const ir_code_t *a, const ir_code_t *b);

/** Separates button presses from the repeats of a held button. */
typedef struct {
    ir_code_t last;   /**< first code of the current press */
    uint32_t last_ms; /**< time of the last frame of the current press */
    bool active;
} ir_press_filter_t;

/** Returns true when the frame is the first code of a new button press: IR_PRESS_GAP_MS or more after the
 *  previous frame. Every frame of a press (repeats, NEC repeat frames, other frames of the same press) only extends
 *  it; noise (IR_FRAME_NONE) is ignored. */
bool ir_press_filter_accept(ir_press_filter_t *filter, ir_frame_t frame, const ir_code_t *code, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
