// FreeRTOS stand-ins with the 100 Hz tick of ESP-IDF (CONFIG_FREERTOS_HZ): a delay ends on a 10 ms tick boundary,
// as on the chip. One task runs at a time (see sim.h).
#pragma once
#include <stdint.h>
#include "sim.h"
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;
typedef void *TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define configTICK_RATE_HZ 100
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000))
#define pdPASS 1
