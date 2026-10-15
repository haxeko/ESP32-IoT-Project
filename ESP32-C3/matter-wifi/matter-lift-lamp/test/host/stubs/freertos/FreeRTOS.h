// Host stub of FreeRTOS.h for the host tests (single-threaded).
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffUL)

/* Tick rate of the firmware (CONFIG_FREERTOS_HZ, 100 in this project's build) and pdMS_TO_TICKS as in FreeRTOS
 * projdefs.h: delays shorter than a tick round down, pdMS_TO_TICKS(2) == 0 at 100 Hz */
#ifndef configTICK_RATE_HZ
#ifdef CONFIG_FREERTOS_HZ
#define configTICK_RATE_HZ CONFIG_FREERTOS_HZ
#else
#define configTICK_RATE_HZ 100
#endif
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(((TickType_t)(ms) * (TickType_t)configTICK_RATE_HZ) / (TickType_t)1000U))
#endif
