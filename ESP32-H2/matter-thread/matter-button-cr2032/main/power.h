#pragma once

/*
 * Power and Thread link settings of the battery button.
 *
 *  - Automatic Light-sleep (esp_pm) with the digital peripherals powered down (sdkconfig).
 *  - 802.15.4 TX power: the ESP-IDF driver starts at the top of the PHY table (+20 dBm, 140 mA on ESP32-H2) and
 *    OpenThread never lowers it; CONFIG_THREAD_TX_POWER_DBM is applied as soon as the driver is initialised
 *    (linker wrap of esp_ieee802154_enable(), before the first frame), then again after every re-attach.
 *  - Child timeout / child supervision large enough for the ICD slow polling interval (OpenThread defaults:
 *    240 s / 129 s / 190 s, fine up to ~30 s polling).
 *  - Network down: OpenThread retries to attach with a backoff up to 20 min (~2 s of RX at 25 mA each time).
 *    After CONFIG_THREAD_DETACHED_PAUSE_MIN detached, Thread is paused and retried with a growing interval
 *    (CONFIG_THREAD_RETRY_MIN_MIN .. CONFIG_THREAD_RETRY_MAX_MIN); a button press retries at once.
 *  - LIT build with DSLS: stay in SIT while any fabric has no registered Check-In client (a controller that does
 *    not understand LIT would see the device as offline).
 */

#include <platform/CHIPDeviceEvent.h>

/** esp_pm configuration: call early in app_main(). */
void power_configure(void);

/** After esp_matter::start(): TX power, child timeout, supervision; ICD policy observer (LIT builds). */
void power_thread_setup(void);

/** Forward the esp_matter device events (CHIP task). */
void power_on_event(const chip::DeviceLayer::ChipDeviceEvent *event);

/** A button press (CHIP task): if Thread is paused or detached for a while, try to attach now. */
void power_on_user_activity(void);
