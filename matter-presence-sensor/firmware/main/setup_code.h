/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Matter setup code (QR code / manual pairing code) of this board.
//
// Default build: every board uses the SDK test setup code (passcode 20202021, discriminator 3840). With
// sdkconfig.defaults.unique_setup (CONFIG_UNIQUE_SETUP_CODE) each board instead generates its own random passcode,
// discriminator and SPAKE2+ salt on first use, computes the SPAKE2+ verifier and keeps them in the otherwise unused
// "fctry" flash partition (namespace "setup-code"): they survive reboots, OTA, a Matter factory reset and flashing
// the merged or app image, and are regenerated only after erase-flash. The setup code is only used to commission
// the board; controllers that already paired are not affected.

#pragma once

#include <esp_err.h>
#include <stdbool.h>

// Installs the per-board setup code provider (CONFIG_UNIQUE_SETUP_CODE). Before esp_matter::start(). Does nothing
// in the default build.
esp_err_t setup_code_init(void);

// Loads (or generates and stores, first boot only) the per-board setup code on the Matter task (8 KB stack) and
// waits for it. After esp_matter::start() and before starting a small task that may read it (id_diag). Does
// nothing in the default build.
void setup_code_preload(void);

// Logs the QR code, its web link and the manual pairing code of this board (CONFIG_PRINT_ONBOARDING_CODES).
// on_network: the board is advertised over DNS-SD only (BLE already released). Matter stack lock held (Matter task
// or ScopedChipStackLock). Only for windows opened with the board's own setup code (basic commissioning window),
// never for a window opened by a controller (Multi-Admin, enhanced window). Never logs the passcode by itself, but
// the QR and manual codes contain it: a development aid.
void setup_code_print(bool on_network);
