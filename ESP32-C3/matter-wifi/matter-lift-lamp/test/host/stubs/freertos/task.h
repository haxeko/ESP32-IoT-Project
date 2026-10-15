// Host stub of FreeRTOS task.h: vTaskDelay() lets the simulated time run (the test defines sim_task_delay(), which
// calls the timer interrupt for every alarm that falls in the delay).
#pragma once

#include "FreeRTOS.h"

void sim_task_delay(TickType_t ticks);

static inline void vTaskDelay(TickType_t ticks)
{
    sim_task_delay(ticks);
}

/* Task creation and notifications: enough to compile main/lift.c; the tests call its lift_pass() directly */
typedef void *TaskHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdPASS 1

static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                                     TaskHandle_t *handle)
{
    (void)fn, (void)name, (void)stack, (void)arg, (void)prio;
    *handle = (TaskHandle_t)1;
    return pdPASS;
}

static inline void xTaskNotifyGive(TaskHandle_t task)
{
    (void)task;
}

static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    (void)clear;
    sim_task_delay(ticks);
    return 0;
}
