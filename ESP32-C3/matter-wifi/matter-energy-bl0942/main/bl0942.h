#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One reading of the BL0942 */
typedef struct {
    bool valid;              /* false when the BL0942 did not answer (wiring / baud problem) */
    float power_w;           /* active power, W */
    float voltage_v;         /* RMS voltage, V */
    float current_a;         /* RMS current, A */
    float frequency_hz;      /* mains frequency, Hz */
    uint64_t energy_mwh;     /* cumulative imported energy since first boot, mWh */
} bl0942_reading_t;

/**
 * Configure the UART (CONFIG_BL0942_*) used to talk to the BL0942.
 *
 * @param energy_base_mwh energy restored from NVS, added to the energy counted by CF_CNT.
 */
esp_err_t bl0942_init(uint64_t energy_base_mwh);

/**
 * Read the full parameter packet from the BL0942 and convert it to physical units.
 * Call periodically (every CONFIG_BL0942_UPDATE_INTERVAL_MS).
 */
void bl0942_update(bl0942_reading_t *out);

#ifdef __cplusplus
}
#endif
