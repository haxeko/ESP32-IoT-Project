#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "sim.h"
typedef void (*esp_timer_cb_t)(void *arg);
typedef enum { ESP_TIMER_TASK } esp_timer_dispatch_t;
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
typedef SimTimer *esp_timer_handle_t;
static inline int64_t esp_timer_get_time(void) { return g_sim.now_us; }
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle)
{
    g_sim.timers.reserve(16); // handles stay valid
    g_sim.timers.push_back({ args->callback, args->arg, 0, 0 });
    *handle = &g_sim.timers.back();
    return ESP_OK;
}
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us)
{
    timer->period_us = (int64_t)period_us;
    timer->next_us = g_sim.now_us + (int64_t)period_us;
    return ESP_OK;
}
