// Host stub of FreeRTOS.h for the host tests (single-threaded).
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
