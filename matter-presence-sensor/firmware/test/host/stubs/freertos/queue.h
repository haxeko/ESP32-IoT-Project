#pragma once
#include <string.h>
#include "freertos/FreeRTOS.h"
typedef SimQueue *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    return new SimQueue{ length, item_size, {} };
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t)
{
    if (q->items.size() >= q->length) {
        return pdFALSE;
    }
    const uint8_t *p = (const uint8_t *)item;
    q->items.push_back(std::vector<uint8_t>(p, p + q->item_size));
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    for (TickType_t waited = 0;; waited++) {
        if (!q->items.empty()) {
            memcpy(item, q->items.front().data(), q->item_size);
            q->items.pop_front();
            return pdTRUE;
        }
        if (waited >= ticks) {
            return pdFALSE;
        }
        sim_advance(1);
    }
}
static inline BaseType_t xQueueReset(QueueHandle_t q)
{
    q->items.clear();
    return pdPASS;
}
