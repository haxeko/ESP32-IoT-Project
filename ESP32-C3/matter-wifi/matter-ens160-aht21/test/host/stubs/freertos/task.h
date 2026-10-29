#pragma once
#include "freertos/FreeRTOS.h"
static constexpr int64_t kSimTickMs = 1000 / configTICK_RATE_HZ;
// Tasks are not started: a test calls the code of the task itself.
static inline BaseType_t xTaskCreate(void (*)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *)
{
    return pdPASS;
}
static inline TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(sim_ms() / kSimTickMs);
}
// Wakes up at the tick boundary ticks after the current tick: between ticks - 1 and ticks periods later.
static inline void vTaskDelay(TickType_t ticks)
{
    if (ticks == 0) {
        return;
    }
    const int64_t wake_ms = (sim_ms() / kSimTickMs + ticks) * kSimTickMs;
    sim_advance_ms((uint32_t)(wake_ms - sim_ms()));
}
static inline void vTaskDelayUntil(TickType_t *last, TickType_t ticks)
{
    *last += ticks;
    const int64_t wake_ms = (int64_t)*last * kSimTickMs;
    if (wake_ms > sim_ms()) {
        sim_advance_ms((uint32_t)(wake_ms - sim_ms()));
    }
}
