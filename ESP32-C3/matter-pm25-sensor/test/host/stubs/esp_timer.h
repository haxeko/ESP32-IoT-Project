#pragma once
#include "esp_err.h"
#include "sim.h"
typedef void (*esp_timer_cb_t)(void *arg);
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    int dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
typedef void *esp_timer_handle_t;
static inline int64_t esp_timer_get_time(void) { return g_sim.now_us; }
// The handle is the 1-based index in g_sim.timers.
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle)
{
    g_sim.timers.push_back({ args->callback, args->arg, 0 });
    *handle = (void *)(intptr_t)g_sim.timers.size();
    return ESP_OK;
}
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us)
{
    g_sim.timers[(intptr_t)handle - 1].period_us = (int64_t)period_us;
    return ESP_OK;
}
static inline esp_err_t esp_timer_delete(esp_timer_handle_t) { return ESP_OK; }
