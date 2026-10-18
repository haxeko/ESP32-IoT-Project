#pragma once
#include "freertos/FreeRTOS.h"
// Tasks are not started: the test runs the task function it wants.
static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *, uint32_t, void *arg, int, TaskHandle_t *)
{
    g_sim.tasks.push_back({ fn, arg });
    return pdPASS;
}
static inline void vTaskDelay(TickType_t ticks) { sim_advance_us((int64_t)ticks * 1000); }
