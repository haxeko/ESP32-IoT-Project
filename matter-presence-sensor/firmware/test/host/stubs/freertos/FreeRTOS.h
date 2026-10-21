// FreeRTOS stand-ins: 1 tick = 1 ms, one task runs at a time (see sim.h).
#pragma once
#include <stdint.h>
#include "sim.h"
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t) ((uint32_t)(t))
#define configTICK_RATE_HZ 1000
#define tskIDLE_PRIORITY 0
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
