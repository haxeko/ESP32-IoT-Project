// Host stub of ESP-IDF driver/gptimer.h: one simulated timer with the state machine of ESP-IDF v6.0.2
// (components/esp_driver_gptimer/src/gptimer.c): init -> enable -> run. As there, gptimer_start() on a running
// timer and gptimer_stop() on a stopped one do nothing and return ESP_OK; they are counted (redundant_starts /
// redundant_stops) so a test can still check the caller's own bookkeeping. Any other wrong state returns
// ESP_ERR_INVALID_STATE. The test moves the time and calls on_alarm at every alarm (count reaches alarm_count).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct gptimer_t *gptimer_handle_t;

typedef struct {
    uint64_t count_value;
    uint64_t alarm_value;
} gptimer_alarm_event_data_t;

typedef bool (*gptimer_alarm_cb_t)(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);

typedef enum { GPTIMER_CLK_SRC_DEFAULT = 0 } gptimer_clock_source_t;
typedef enum { GPTIMER_COUNT_DOWN = 0, GPTIMER_COUNT_UP = 1 } gptimer_count_direction_t;

typedef struct {
    gptimer_clock_source_t clk_src;
    gptimer_count_direction_t direction;
    uint32_t resolution_hz;
    int intr_priority;
    struct {
        uint32_t intr_shared : 1;
        uint32_t allow_pd : 1;
    } flags;
} gptimer_config_t;

typedef struct {
    gptimer_alarm_cb_t on_alarm;
} gptimer_event_callbacks_t;

typedef struct {
    uint64_t alarm_count;
    uint64_t reload_count;
    struct {
        uint32_t auto_reload_on_alarm : 1;
    } flags;
} gptimer_alarm_config_t;

typedef enum { SIM_GPTIMER_FREE = 0, SIM_GPTIMER_INIT, SIM_GPTIMER_ENABLE, SIM_GPTIMER_RUN } sim_gptimer_fsm_t;

struct gptimer_t {
    sim_gptimer_fsm_t fsm;
    uint32_t resolution_hz;
    gptimer_alarm_cb_t on_alarm;
    void *user_ctx;
    bool alarm_set;
    bool auto_reload;
    uint64_t alarm_count;
    uint64_t reload_count;
    uint64_t count; /* counter value, kept while stopped (as the hardware does) */
    int starts, stops, redundant_starts, redundant_stops;
};

/* The only timer; the test defines it and resets it (memset 0) for every simulated boot */
extern struct gptimer_t g_sim_gptimer;

static inline esp_err_t gptimer_new_timer(const gptimer_config_t *config, gptimer_handle_t *ret)
{
    if (!config || !ret || config->resolution_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_sim_gptimer.fsm != SIM_GPTIMER_FREE) {
        return ESP_ERR_NOT_FOUND; /* all timers used */
    }
    g_sim_gptimer.fsm = SIM_GPTIMER_INIT;
    g_sim_gptimer.resolution_hz = config->resolution_hz;
    *ret = &g_sim_gptimer;
    return ESP_OK;
}

static inline esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *cbs,
                                                         void *user_data)
{
    if (!t || !cbs) {
        return ESP_ERR_INVALID_ARG;
    }
    if (t->fsm != SIM_GPTIMER_INIT) {
        return ESP_ERR_INVALID_STATE;
    }
    t->on_alarm = cbs->on_alarm;
    t->user_ctx = user_data;
    return ESP_OK;
}

static inline esp_err_t gptimer_enable(gptimer_handle_t t)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    if (t->fsm != SIM_GPTIMER_INIT) {
        return ESP_ERR_INVALID_STATE;
    }
    t->fsm = SIM_GPTIMER_ENABLE;
    return ESP_OK;
}

static inline esp_err_t gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *config)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config) {
        if (config->flags.auto_reload_on_alarm && config->alarm_count == config->reload_count) {
            return ESP_ERR_INVALID_ARG;
        }
        t->alarm_set = true;
        t->auto_reload = config->flags.auto_reload_on_alarm;
        t->alarm_count = config->alarm_count;
        t->reload_count = config->reload_count;
    } else {
        t->alarm_set = false;
    }
    return ESP_OK;
}

static inline esp_err_t gptimer_start(gptimer_handle_t t)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    if (t->fsm == SIM_GPTIMER_RUN) {
        t->redundant_starts++;
        return ESP_OK;
    }
    if (t->fsm != SIM_GPTIMER_ENABLE) {
        return ESP_ERR_INVALID_STATE;
    }
    t->fsm = SIM_GPTIMER_RUN;
    t->starts++;
    return ESP_OK;
}

static inline esp_err_t gptimer_stop(gptimer_handle_t t)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    if (t->fsm == SIM_GPTIMER_ENABLE) {
        t->redundant_stops++;
        return ESP_OK;
    }
    if (t->fsm != SIM_GPTIMER_RUN) {
        return ESP_ERR_INVALID_STATE;
    }
    t->fsm = SIM_GPTIMER_ENABLE;
    t->stops++;
    return ESP_OK;
}
