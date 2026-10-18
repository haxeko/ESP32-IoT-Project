#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
// The probe's output buffer: each record goes straight to g_sim.records.
typedef void *RingbufHandle_t;
#define RINGBUF_TYPE_NOSPLIT 0
static inline RingbufHandle_t xRingbufferCreate(size_t, int) { return (void *)1; }
static inline BaseType_t xRingbufferSend(RingbufHandle_t, const void *data, size_t len, TickType_t)
{
    sim_record((const char *)data, len);
    return pdTRUE;
}
static inline void *xRingbufferReceive(RingbufHandle_t, size_t *, TickType_t) { return nullptr; }
static inline void vRingbufferReturnItem(RingbufHandle_t, void *) {}
