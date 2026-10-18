#pragma once
#include "freertos/FreeRTOS.h"
typedef struct SimQueue *QueueHandle_t;
QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t q);
