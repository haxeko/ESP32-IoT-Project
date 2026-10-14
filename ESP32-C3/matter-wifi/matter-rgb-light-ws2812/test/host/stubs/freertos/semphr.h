// Host stub of FreeRTOS semphr.h: a mutex that checks it is never taken twice or given without being taken.
#pragma once

#include <assert.h>
#include <stdbool.h>

#include "FreeRTOS.h"

typedef struct {
    bool taken;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf)
{
    buf->taken = false;
    return buf;
}

static inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    (void)wait;
    assert(s && !s->taken);
    s->taken = true;
    return 1;
}

static inline int xSemaphoreGive(SemaphoreHandle_t s)
{
    assert(s && s->taken);
    s->taken = false;
    return 1;
}
