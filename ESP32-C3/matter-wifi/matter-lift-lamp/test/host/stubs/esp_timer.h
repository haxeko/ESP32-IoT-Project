// Host stub of ESP-IDF esp_timer.h: timers never fire by themselves, the test calls their callbacks
// (sim_timer_fire) and moves the clock (g_sim_now_us).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*esp_timer_cb_t)(void *arg);
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
} esp_timer_create_args_t;
typedef struct sim_timer *esp_timer_handle_t;

struct sim_timer {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
    bool running;
    bool periodic;
    uint64_t period_us;
};

#define SIM_MAX_TIMERS 4
extern struct sim_timer g_sim_timers[SIM_MAX_TIMERS];
extern int g_sim_timer_count;
extern int64_t g_sim_now_us;

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (g_sim_timer_count >= SIM_MAX_TIMERS) {
        return ESP_ERR_NO_MEM;
    }
    struct sim_timer *t = &g_sim_timers[g_sim_timer_count++];
    t->callback = args->callback;
    t->arg = args->arg;
    t->name = args->name;
    *out = t;
    return ESP_OK;
}

static inline esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    t->running = true;
    t->periodic = false;
    t->period_us = timeout_us;
    return ESP_OK;
}

static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    t->running = true;
    t->periodic = true;
    t->period_us = period_us;
    return ESP_OK;
}

static inline esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!t->running) {
        return ESP_ERR_INVALID_STATE;
    }
    t->running = false;
    return ESP_OK;
}

static inline int64_t esp_timer_get_time(void)
{
    return g_sim_now_us;
}
