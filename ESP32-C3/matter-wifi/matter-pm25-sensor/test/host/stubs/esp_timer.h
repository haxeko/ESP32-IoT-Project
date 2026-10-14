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
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle)
{
    g_sim.timer_cb = args->callback;
    *handle = (void *)1;
    return ESP_OK;
}
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t period_us)
{
    g_sim.timer_period_us = (int64_t)period_us;
    return ESP_OK;
}
static inline esp_err_t esp_timer_delete(esp_timer_handle_t) { return ESP_OK; }
