#pragma once
#include "freertos/FreeRTOS.h"
// Tasks are not started: a test calls the task function itself.
static inline BaseType_t xTaskCreate(void (*)(void *), const char *name, uint32_t, void *, UBaseType_t,
                                     TaskHandle_t *handle)
{
    g_sim.tasks.push_back(name);
    if (handle) {
        *handle = (TaskHandle_t)(uintptr_t)g_sim.tasks.size();
    }
    return pdPASS;
}
static inline TickType_t xTaskGetTickCount(void) { return (TickType_t)sim_ms(); }
static inline void vTaskDelay(TickType_t ticks) { sim_advance(ticks); }
static inline void vTaskDelayUntil(TickType_t *last, TickType_t ticks)
{
    *last += ticks;
    const int64_t wait = (int64_t)*last - sim_ms();
    if (wait > 0) {
        sim_advance((uint32_t)wait);
    }
}
static inline void xTaskNotifyGive(TaskHandle_t) { g_sim.notify++; }
static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    for (TickType_t waited = 0;; waited++) {
        if (g_sim.notify > 0) {
            const uint32_t n = g_sim.notify;
            g_sim.notify = clear ? 0 : n - 1;
            return n;
        }
        if (waited >= ticks) {
            return 0;
        }
        sim_advance(1);
    }
}
