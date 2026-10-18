#pragma once

#include <platform/CHIPDeviceLayer.h>

// Logs why the chip started: power on, or how the previous run ended (watchdog, crash, brownout...).
void app_health_log_reset_reason();

// Watches the Matter thread and logs a status line every 60 s (heap, Thread role, OMR address, SRP registration,
// subscriptions per fabric). Call once after esp_matter::start().
// The Matter thread must run a small work item every 5 s; if it stops (deadlock, endless loop), the task watchdog
// restarts the device after CONFIG_ESP_TASK_WDT_TIMEOUT_S instead of leaving it unresponsive until unplugged.
void app_health_start();

// Matter device events (Matter thread): identity and fabric logs, Thread attach/detach logs.
void app_health_handle_event(const chip::DeviceLayer::ChipDeviceEvent *event);
