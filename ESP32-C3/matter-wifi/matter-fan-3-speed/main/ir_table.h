#pragma once

/* Learned IR remote buttons and what they do. Pure C, no ESP-IDF dependency (tested on the host). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fan_logic.h"
#include "ir_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Remote functions, in the order the learning wizard asks for them. Stored in NVS by index: append only. */
typedef enum {
    IR_FUNC_POWER = 0, /**< on/off; "on" only when an OFF button is learned too (remotes with separate on/off) */
    IR_FUNC_SPEED,     /**< next speed 1 -> 2 -> 3 -> 1, turns the fan on when off */
    IR_FUNC_SWING,     /**< oscillation on/off */
    IR_FUNC_OFF,       /**< off */
    IR_FUNC_SPEED_1,   /**< speed 1 */
    IR_FUNC_SPEED_2,   /**< speed 2 */
    IR_FUNC_SPEED_3,   /**< speed 3 */
    IR_FUNC_NATURAL,   /**< natural wind on/off ("rhythm" / "nature" / "mode" button of fan remotes) */
    IR_FUNC_COUNT
} ir_func_t;

typedef struct {
    ir_code_t codes[IR_FUNC_COUNT]; /**< proto IR_PROTO_NONE = not learned */
} ir_table_t;

/** Function of a received code, or -1 when the code is not learned. */
int ir_table_find(const ir_table_t *table, const ir_code_t *code);

/** Learn a code for a function. A code drives one function only: it is removed from any other function. */
void ir_table_assign(ir_table_t *table, int func, const ir_code_t *code);

/** Number of learned functions. */
int ir_table_count(const ir_table_t *table);

/** Fan command of a learned function. Returns false for an invalid function. */
bool ir_table_command(const ir_table_t *table, int func, fan_cmd_t *cmd);

/** Short English name of a function, for logs. */
const char *ir_func_name(int func);

/* NVS record of the learned codes (ir_rx.cpp): the version (uint32), then the code of each function in ir_func_t
 * order (ir_code_t layout, 8 bytes each), little-endian. Functions are only appended: a record of a newer version
 * starts with the codes this firmware knows (e.g. after an OTA rollback). Version 1 had the 7 functions before
 * NATURAL. */
#define IR_RECORD_VERSION 2
#define IR_RECORD_V1_FUNCTIONS 7
#define IR_RECORD_SIZE (4 + 8 * IR_FUNC_COUNT)
/** Longest record accepted (from a newer firmware) */
#define IR_RECORD_MAX_SIZE 1024

/** Encode the table into buf (IR_RECORD_SIZE bytes). */
void ir_record_encode(const ir_table_t *table, uint8_t buf[IR_RECORD_SIZE]);

/** Decode a stored record of len bytes into *table (functions the record does not have are cleared). Returns false,
 *  leaving *table unchanged, for an unknown record. */
bool ir_record_decode(const uint8_t *buf, size_t len, ir_table_t *table);

#ifdef __cplusplus
}
#endif
