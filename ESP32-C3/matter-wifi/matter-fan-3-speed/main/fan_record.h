#pragma once

/* NVS record of the fan state (fan_store.cpp). Pure C, no ESP-IDF dependency (tested on the host).
 *
 * Layout, one byte each: version, speed, last speed, oscillation, natural wind. Records are append-only: a newer
 * firmware may add fields at the end (with a higher version) but never changes the existing ones, so an older
 * firmware still reads the fields it knows (e.g. after an OTA rollback). Version 1 was the first 4 bytes (no natural
 * wind byte). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fan_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_RECORD_VERSION 2
#define FAN_RECORD_SIZE 5
#define FAN_RECORD_V1_SIZE 4
/** Longest record accepted (from a newer firmware) */
#define FAN_RECORD_MAX_SIZE 64

/** Encode the state into buf (FAN_RECORD_SIZE bytes). */
void fan_record_encode(const fan_state_t *state, uint8_t buf[FAN_RECORD_SIZE]);

/** Decode a stored record of len bytes into *state (sanitized). Returns false, leaving *state unchanged, for an
 *  unknown record: empty, an older layout of the wrong size, or a current or newer version that is too short. */
bool fan_record_decode(const uint8_t *buf, size_t len, fan_state_t *state);

#ifdef __cplusplus
}
#endif
