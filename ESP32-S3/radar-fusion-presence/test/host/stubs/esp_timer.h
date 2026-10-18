#pragma once
#include <stdint.h>
#include "sim_probe.h"
static inline int64_t esp_timer_get_time(void) { return g_sim.now_us; }
