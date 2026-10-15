// Host stub of FreeRTOS task.h: vTaskDelay() lets the simulated time run (the test defines sim_task_delay(), which
// calls the timer interrupt for every alarm that falls in the delay).
#pragma once

#include "FreeRTOS.h"

void sim_task_delay(TickType_t ticks);

static inline void vTaskDelay(TickType_t ticks)
{
    sim_task_delay(ticks);
}
