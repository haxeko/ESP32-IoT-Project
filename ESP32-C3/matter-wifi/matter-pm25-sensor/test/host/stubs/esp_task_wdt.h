#pragma once
#include "esp_err.h"
#include "sim.h"
static inline esp_err_t esp_task_wdt_add(void *) { return ESP_OK; }
static inline esp_err_t esp_task_wdt_reset(void) { g_sim.wdt_resets++; return ESP_OK; }
