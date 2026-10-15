#pragma once

/*
 * Identity / Thread / Matter diagnostics (log tag "id_diag"), for telling apart the layers when several
 * boards with the same firmware share a Thread network.
 *
 * One block at boot and then every period (Kconfig IDENTITY_DIAG_PERIOD_SEC, 0 = off), plus short lines on
 * events (Thread role / connectivity change, CASE session, fabric added or removed, subscriptions):
 *   - reset reason, uptime, free heap
 *   - factory MAC (eFuse) and IEEE 802.15.4 EUI-64
 *   - VID/PID, SerialNumber, UniqueID, discriminator, whether the SDK test setup code is compiled in
 *   - Thread: role, RLOC16, extended address (source of the SRP / DNS-SD host name), ML-EID, partition ID,
 *     leader, network name, Ext PAN ID, PAN ID, channel, neighbours, parent link, MLE counters, on-mesh
 *     prefixes, SRP server / host / service state, OMR address
 *   - Matter: commissioned, commissioning window, fail-safe, sessions, subscriptions, and per fabric:
 *     fabric index, node ID, fabric ID, compressed fabric ID, controller vendor ID, label
 *   - NVS usage (entries per namespace, never values)
 * Never logged: setup passcode, DAC / operational private keys, IPK, Thread network key, PSKc.
 *
 * Locks: CHIP data is copied with the CHIP stack lock held, OpenThread data with the OpenThread lock held,
 * never both at once (the CHIP task itself takes CHIP -> OpenThread; the reverse order could deadlock).
 */

#include <stdint.h>

#include <esp_err.h>
#include <platform/CHIPDeviceEvent.h>

/** Start the diagnostics task (after esp_matter::start()). period_s = 0: feature off. */
esp_err_t identity_diag_start(uint32_t period_s);

/** Forward every event of the esp_matter app event callback (CHIP task, CHIP lock held). Never blocks. */
void identity_diag_on_event(const chip::DeviceLayer::ChipDeviceEvent *event);

/** Ask for a full block as soon as possible (any task, non-blocking). */
void identity_diag_request_dump(void);
