#pragma once

#include <platform/CHIPDeviceLayer.h>

/** Log why the chip started: power on, or how the previous run ended (watchdog, crash, brownout...). */
void app_health_log_reset_reason();

/**
 * Watch the Matter thread and log a status line every 60 s. Call once after esp_matter::start().
 * The Matter thread must run a small work item every 5 s; if it stops (deadlock, endless loop), the task watchdog
 * restarts the device after CONFIG_ESP_TASK_WDT_TIMEOUT_S instead of leaving it unresponsive until unplugged.
 * Also applies the Wi-Fi settings below if Wi-Fi started before the app event callback was registered.
 */
void app_health_start();

/**
 * Matter device events (Matter thread): Wi-Fi transmit power (CONFIG_APP_WIFI_TX_POWER), Wi-Fi power save off
 * once BLE is off (CONFIG_APP_WIFI_POWER_SAVE_OFF), Wi-Fi disconnect logs.
 */
void app_health_handle_event(const chip::DeviceLayer::ChipDeviceEvent *event);
