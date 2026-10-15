#pragma once

/*
 * Matter setup code (QR code / manual pairing code) of this board.
 *
 * Default build: every board uses the SDK test setup code (passcode 20202021, discriminator 3840), the same
 * as before. With sdkconfig.defaults.unique_setup (CONFIG_UNIQUE_SETUP_CODE) each board instead generates its
 * own random passcode, discriminator and SPAKE2+ salt on first use, computes the SPAKE2+ verifier and keeps
 * them in the unused "fctry" flash partition (namespace "setup-code"): they survive reboots, OTA, a Matter
 * factory reset and flashing the merged / app image, and are regenerated only after erase-flash.
 * The setup code is used only to commission the board; already commissioned controllers are not affected.
 */

#include <stdbool.h>

#include <esp_err.h>

/** Install the per-board setup code provider (CONFIG_UNIQUE_SETUP_CODE). Call before esp_matter::start().
 *  Does nothing in the default build. */
esp_err_t setup_code_init(void);

/** Load (or generate and store, first boot only) the per-board setup code on the Matter task and wait for it.
 *  Call after esp_matter::start() and before starting any small task that may read it (identity_diag).
 *  Does nothing in the default build. */
void setup_code_preload(void);

/** Log the QR code, its web link and the manual pairing code of this board (CONFIG_PRINT_ONBOARDING_CODES).
 *  on_network: the board is advertised over DNS-SD only (BLE already released). Call with the CHIP stack lock
 *  held (Matter task or ScopedChipStackLock), only for windows opened with the board's own setup code (basic
 *  commissioning window), not for a window opened by a controller (Multi-Admin, enhanced window). */
void setup_code_print(bool on_network);
