#pragma once
#include "freertos/FreeRTOS.h"
// One task at a time: the mutex is always free.
typedef int *SemaphoreHandle_t;
static int s_sim_mutex;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_sim_mutex; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
