#pragma once
#include <stdint.h>
#include "sim_probe.h"
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
// CONFIG_FREERTOS_HZ=1000: one tick per millisecond
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY 0xFFFFFFFFu
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
