/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Frame decoding and unit conversions of the ENS160 + AHT21 module, without ESP-IDF or Matter dependencies, so that
// the host tests (test/host) can check them.

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------------------------------------------------
// AHT21 (AHT20 and AHT25 use the same protocol)

// CRC-8 of the AHT2x frames: polynomial 0x31 (x^8 + x^5 + x^4 + 1), initial value 0xFF, no reflection
// (CRC-8/NRSC-5, the one of the Sensirion sensors too).
inline uint8_t aht2x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// Status byte: bit 7 busy (measuring), bit 3 calibrated. Bits 3 and 4 both set after power-on means ready; otherwise
// the registers 0x1B, 0x1C and 0x1E need the initialization of the datasheet.
constexpr uint8_t kAht2xStatusBusy = 0x80;
constexpr uint8_t kAht2xStatusCalibrated = 0x08;
constexpr uint8_t kAht2xStatusReady = 0x18;

typedef enum {
    AHT2X_FRAME_OK,
    AHT2X_FRAME_BUSY,           // measurement not finished: read again
    AHT2X_FRAME_BAD_CRC,        // corrupted on the bus
    AHT2X_FRAME_NOT_CALIBRATED, // the sensor lost its initialization (power glitch): initialize it again
} aht2x_frame_t;

// Measurement frame of 7 bytes: status, humidity (20 bits), temperature (20 bits), CRC of the first 6 bytes.
//   RH = raw / 2^20 * 100 %, T = raw / 2^20 * 200 - 50 °C
inline aht2x_frame_t aht2x_parse(const uint8_t frame[7], float *temperature_c, float *humidity_pct)
{
    if (frame[0] & kAht2xStatusBusy) {
        return AHT2X_FRAME_BUSY;
    }
    if (aht2x_crc8(frame, 6) != frame[6]) {
        return AHT2X_FRAME_BAD_CRC;
    }
    if (!(frame[0] & kAht2xStatusCalibrated)) {
        return AHT2X_FRAME_NOT_CALIBRATED;
    }
    const uint32_t rh = ((uint32_t)frame[1] << 12) | ((uint32_t)frame[2] << 4) | (frame[3] >> 4);
    const uint32_t t = (((uint32_t)frame[3] & 0x0F) << 16) | ((uint32_t)frame[4] << 8) | frame[5];
    *humidity_pct = (float)rh * (100.0f / 1048576.0f);
    *temperature_c = (float)t * (200.0f / 1048576.0f) - 50.0f;
    return AHT2X_FRAME_OK;
}

// ---------------------------------------------------------------------------------------------------------------------
// ENS160

// DEVICE_STATUS (0x20)
constexpr uint8_t kEns160StatusRunning = 0x80; // STATAS: an operating mode is running
constexpr uint8_t kEns160StatusError = 0x40;   // STATER: error, e.g. invalid operating mode
constexpr uint8_t kEns160StatusNewData = 0x02; // NEWDAT: new values in DATA_AQI, DATA_TVOC, DATA_ECO2

// Validity flag, bits 3:2 of DEVICE_STATUS
typedef enum {
    ENS160_VALIDITY_NORMAL = 0,
    ENS160_VALIDITY_WARM_UP = 1,          // first 3 minutes after each power-on
    ENS160_VALIDITY_INITIAL_START_UP = 2, // first hour of operation of a new sensor
    ENS160_VALIDITY_INVALID = 3,
} ens160_validity_t;

inline ens160_validity_t ens160_validity(uint8_t status)
{
    return (ens160_validity_t)((status >> 2) & 0x03);
}

inline const char *ens160_validity_name(ens160_validity_t validity)
{
    switch (validity) {
    case ENS160_VALIDITY_NORMAL:
        return "normal operation";
    case ENS160_VALIDITY_WARM_UP:
        return "warm-up, about 3 min after power-on";
    case ENS160_VALIDITY_INITIAL_START_UP:
        return "initial start-up, first hour of a new sensor";
    default:
        return "invalid output";
    }
}

// Compensation inputs: TEMP_IN = Kelvin x 64, RH_IN = %RH x 512 (16 bits each, LSB first on the bus).
inline uint16_t ens160_temp_in(float celsius)
{
    const float value = (celsius + 273.15f) * 64.0f;
    if (!(value > 0.0f)) {
        return 0; // also NaN
    }
    if (value >= 65535.0f) {
        return 65535;
    }
    return (uint16_t)(value + 0.5f);
}

inline uint16_t ens160_rh_in(float humidity_pct)
{
    if (!(humidity_pct > 0.0f)) {
        return 0;
    }
    if (humidity_pct >= 100.0f) {
        return 51200;
    }
    return (uint16_t)(humidity_pct * 512.0f + 0.5f);
}

// Matter AirQualityEnum (Air Quality cluster) for the AQI-UBA index of the ENS160 (German Federal Environmental
// Agency): 1 excellent, 2 good, 3 moderate, 4 poor, 5 unhealthy. Anything else is Unknown (0).
//   1 -> Good (1), 2 -> Fair (2), 3 -> Moderate (3), 4 -> Poor (4), 5 -> VeryPoor (5)
inline uint8_t ens160_aqi_to_air_quality(uint8_t aqi_uba)
{
    return aqi_uba >= 1 && aqi_uba <= 5 ? aqi_uba : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Matter values

// Temperature Measurement MeasuredValue: 0.01 °C, rounded and kept within [min, max] (a NaN gives min).
inline int16_t celsius_to_measured(float celsius, int16_t min, int16_t max)
{
    const float value = celsius * 100.0f;
    if (!(value > (float)min)) {
        return min;
    }
    if (value >= (float)max) {
        return max;
    }
    return (int16_t)lroundf(value);
}

// Relative Humidity Measurement MeasuredValue: 0.01 %, from 0 to 10000 (a NaN gives 0).
inline uint16_t humidity_to_measured(float humidity_pct)
{
    const float value = humidity_pct * 100.0f;
    if (!(value > 0.0f)) {
        return 0;
    }
    if (value >= 10000.0f) {
        return 10000;
    }
    return (uint16_t)lroundf(value);
}

// Saturation vapour pressure over water in hPa (Magnus formula, coefficients of Sonntag 1990).
inline float saturation_vapour_pressure(float celsius)
{
    return 6.112f * expf(17.62f * celsius / (243.12f + celsius));
}

// Relative humidity of the same air (same amount of water vapour) at another temperature. Used with a temperature
// offset: a sensor warmed by the ENS160 next to it reads too warm and, for the same air, too dry.
inline float humidity_at_temperature(float humidity_pct, float measured_c, float actual_c)
{
    const float value = humidity_pct * saturation_vapour_pressure(measured_c) / saturation_vapour_pressure(actual_c);
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    return value > 100.0f ? 100.0f : value;
}

// Last value written to a Matter attribute, to skip the writes that would only report noise to the controllers.
struct ReportedValue {
    bool written = false; // nothing written yet
    bool null = false;
    int32_t value = 0;

    // True when the new value (null, or value) must be written: first write, null <-> number, or a number that moved
    // by step or more since the last write.
    bool due(bool now_null, int32_t now, int32_t step) const
    {
        if (!written || now_null != null) {
            return true;
        }
        return !now_null && abs(now - value) >= step;
    }

    void set(bool now_null, int32_t now)
    {
        written = true;
        null = now_null;
        value = now_null ? 0 : now;
    }
};
