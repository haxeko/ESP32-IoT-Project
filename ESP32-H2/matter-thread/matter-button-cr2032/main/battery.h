#pragma once

/*
 * CR2032 monitor.
 *
 * VBAT (after the reverse-polarity MOSFET, before the TPS63900) reaches an ADC pin through 1 MOhm with 100 nF to
 * GND: no divider, so nothing drains the cell except the pin leakage (<= 50 nA, ESP32-H2 datasheet IIL/IIH). The
 * 1 MOhm also limits the current into the pin while VDD is not up yet. The ADC unit is created and deleted for each
 * measurement (it holds no power-domain reference in between) under a PM lock: no Light-sleep in a conversion.
 *
 * Two quantities:
 *  - rest voltage: the node is RC-filtered (100 ms), so it reads the voltage at the sleep load
 *  - burst voltage: the cell is loaded with a known CPU load for CONFIG_BATTERY_TEST_LOAD_MS and the drop gives
 *    its internal resistance, extrapolated to the ~40 mA a Thread re-attach or BLE commissioning draws from it.
 *    A CR2032's rest voltage stays flat while its internal resistance climbs, so the burst voltage is what
 *    predicts brownouts. BatChargeLevel comes from it; BatPercentRemaining is a coarse rest-voltage estimate.
 *
 * Reporting (a CR2032 reads low for minutes after radio activity):
 *  - The boot reading (loaded, C9 still charging) is for the boot guard only. After a battery change the values
 *    reported before (saved in NVS) are shown until the first rest reading.
 *  - Rest readings wait until no commissioning window or OTA download is running and nothing loaded the cell
 *    (boot, Thread attach, button report) for CONFIG_BATTERY_QUIET_MIN.
 *  - A same or higher percentage is reported at once; a lower one once CONFIG_BATTERY_DROP_CONFIRM_COUNT readings
 *    in a row are lower (taken every CONFIG_BATTERY_CONFIRM_INTERVAL_MIN while confirming), see battery_report.h.
 */

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_matter.h>

enum class BatteryLevel : uint8_t { Ok = 0, Warning = 1, Critical = 2 }; // = Matter BatChargeLevelEnum

/** ADC calibration and a first (loaded: the CPU runs) measurement for the boot guard. Before esp_matter::start(). */
void battery_init(void);

/** A battery too weak to start Matter (or a brownout reset loop): Deep-sleep for CONFIG_BATTERY_RECOVERY_SLEEP_S,
 *  woken earlier by a press (external pull-up builds, button released when going to sleep), instead of draining it
 *  with boot attempts. Returns when booting may continue. Call after button_gpio_init(). */
void battery_boot_guard(void);

/** Power Source device type and cluster (Battery + Replaceable) on endpoint 0, with the values saved in NVS (after
 *  nvs_flash_init()). Before esp_matter::start(). */
esp_err_t battery_create_power_source(esp_matter::node_t *node);

/** After esp_matter::start(): publish the values and start the periodic measurements. */
void battery_start(void);

BatteryLevel battery_level(void);
uint32_t battery_rest_mv(void);

/** Matter OTA download allowed (battery OK). */
bool battery_ota_allowed(void);

/** Long radio activity that loads the cell: rest readings wait until it ends, plus CONFIG_BATTERY_QUIET_MIN. */
enum class BatteryBusy : uint8_t { CommissioningWindow = 0, OtaDownload = 1 };
/** CHIP task. */
void battery_set_busy(BatteryBusy what, bool busy);
/** A short burst (Thread attach, button report): the next rest reading waits CONFIG_BATTERY_QUIET_MIN. CHIP task. */
void battery_note_activity(void);
