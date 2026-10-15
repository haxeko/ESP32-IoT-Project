#pragma once

/* Fan state in NVS: namespace "fan" (shared with the learned IR codes), key "state". A factory reset keeps it.
 * Record layout and versions: fan_record.h. */

#include <esp_err.h>

#include <fan_logic.h>

/** Load the stored state into *state (sanitized). Returns false, leaving *state as it is, when nothing valid is
 *  stored. */
bool fan_store_load(fan_state_t *state);

/** Write the state. Logs and returns the error on failure. */
esp_err_t fan_store_write(const fan_state_t *state);
