#pragma once
#include <stdint.h>
#include "sim.h"
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define pdTRUE 1
// Tasks are not started: each test calls the task function itself.
static inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, TaskHandle_t *handle)
{
    if (handle) {
        *handle = (void *)1;
    }
    return pdPASS;
}
static inline void vTaskDelay(int ms) { sim_advance(ms); }
static inline uint32_t ulTaskNotifyTake(int, int ms) { sim_advance(ms); return 0; }
static inline void xTaskNotifyGive(TaskHandle_t) {}
