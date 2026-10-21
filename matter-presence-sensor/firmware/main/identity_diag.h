/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Identity / network / Matter diagnostics (log tag "id_diag"), to tell the layers apart when several boards with the
// same firmware share a network.
//
// One block at boot, then every period (Kconfig IDENTITY_DIAG_PERIOD_SEC, 0 = off) and after events (Wi-Fi
// connectivity or IP address change, Thread role or connectivity change, commissioning complete, fabric added,
// removed or updated, server ready), coalesced at least 5 s apart; short lines for each Wi-Fi join or failure,
// CASE/PASE session and subscription:
//   - hw:   factory MAC (eFuse), Wi-Fi station MAC or IEEE 802.15.4 EUI-64, reset reason, uptime, free heap
//   - dev:  VID/PID, SerialNumber, UniqueID, discriminator, whether the SDK test setup code is compiled in
//   - mtr:  commissioned, fabrics, commissioning window, fail-safe, CASE/PASE sessions, subscriptions, reads,
//           counters since boot
//   - fab:  per fabric: fabric index, controller vendor ID, node ID, fabric ID, compressed fabric ID, label
//   - wifi: connected, SSID, BSSID, channel, RSSI, power save (ESP32-C3)
//   - ip:   IPv4 address, IPv6 address count per kind (link-local, unique local, global) (ESP32-C3)
//   - ot, mle, br, srp: Thread role, RLOC16, extended address, ML-EID, network, neighbours, parent, MLE counters,
//           on-mesh prefixes and OMR address, SRP server and registration (ESP32-H2)
//   - nvs:  NVS usage (entries per namespace, never values)
// Never logged: setup passcode, DAC / operational private keys, IPK, Thread network key, PSKc, Wi-Fi password.
//
// Locks: the CHIP data is copied with the CHIP stack lock, the Thread data with the OpenThread lock, never both at
// once (the CHIP task itself takes CHIP -> OpenThread; the reverse order could deadlock). Each is waited for at most
// 2 s (a stuck task makes the round log a warning instead). The Wi-Fi data is read without any lock.

#pragma once

#include <stdint.h>

#include <esp_err.h>
#include <platform/CHIPDeviceEvent.h>

// Starts the diagnostics task (priority 1, after esp_matter::start()). period_s = 0: off.
esp_err_t identity_diag_start(uint32_t period_s);

// Every event of the esp_matter app event callback (Matter task, CHIP lock held). Never blocks.
void identity_diag_on_event(const chip::DeviceLayer::ChipDeviceEvent *event);
