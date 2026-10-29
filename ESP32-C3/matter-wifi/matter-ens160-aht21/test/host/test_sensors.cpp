// Sensor task (main/sensors.cpp) with both drivers on the simulated bus: one reading every 10 s, as the task does.
// Built three times (run.sh): defaults; offset -2.0 °C with the ENS160 values sent while warming up; no AHT21.
#include <math.h>
#include <sdkconfig.h>

#include "sensor_math.h"
#include "sensors.h"
#include "sim.h"

#if CONFIG_ENS160_REPORT_WHILE_WARMING_UP
static constexpr bool kReportWhileWarmingUp = true;
#else
static constexpr bool kReportWhileWarmingUp = false;
#endif
static constexpr float kOffset = CONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS / 10.0f;

static sensor_values_t read_after(uint32_t ms)
{
    sim_advance_ms(ms);
    sensor_values_t v;
    sensors_read_once(&v);
    return v;
}

// Temperature and humidity of the simulated AHT21 (raw), as the task reports them
static float expected_temperature()
{
    return g_sim.aht.temperature_raw * 200.0f / 1048576.0f - 50.0f + kOffset;
}

static float expected_humidity()
{
    const float raw_t = g_sim.aht.temperature_raw * 200.0f / 1048576.0f - 50.0f;
    const float raw_rh = g_sim.aht.humidity_raw * 100.0f / 1048576.0f;
    return humidity_at_temperature(raw_rh, raw_t, raw_t + kOffset);
}

[[maybe_unused]] static void test_bus()
{
    CHECK(sensors_init_bus() == ESP_OK);
    CHECK(g_sim.bus_created);
    CHECK(g_sim.sda == 5 && g_sim.scl == 6);
    CHECK(g_sim.internal_pullup);
}

[[maybe_unused]] static void test_start_and_warm_up()
{
    sensor_values_t v = read_after(100);
    CHECK(v.climate_valid);
    CHECK_NEAR(v.temperature_c, expected_temperature(), 0.001);
    CHECK_NEAR(v.humidity_pct, expected_humidity(), 0.01);
    // The ENS160 just started: found, no values in its first second
    CHECK(v.air_found && !v.air_valid);
    sensors_log(&v);
    CHECK(sim_log_contains(kOffset == 0 ? "sensors: 30.00 C, 50.00 %RH | ENS160 starting"
                                        : "sensors: 28.00 C, 56.13 %RH | ENS160 starting"));
    // It got the temperature and humidity of the AHT21 (offset applied) before measuring
    CHECK(g_sim.ens.compensation_writes == 1);
    CHECK(g_sim.ens.temp_in == ens160_temp_in(expected_temperature()));
    CHECK(g_sim.ens.rh_in == ens160_rh_in(expected_humidity()));

    v = read_after(10000);
    CHECK(v.air_found);
    CHECK(v.air_valid == kReportWhileWarmingUp);
    CHECK(sim_log_contains("ENS160: warm-up"));
    sensors_log(&v);
    if (kReportWhileWarmingUp) {
        CHECK(v.aqi == 2 && v.tvoc_ppb == 120 && v.eco2_ppm == 650);
        CHECK(sim_log_contains("| AQI 2, TVOC 120 ppb, eCO2 650 ppm"));
    } else {
        CHECK(sim_log_contains("sent once the values are valid"));
        CHECK(sim_log_contains("| AQI 2, TVOC 120 ppb, eCO2 650 ppm (not valid yet, not sent)"));
    }

    // 3 minutes after the start of the standard mode
    for (int i = 0; i < 18; i++) {
        v = read_after(10000);
    }
    CHECK(v.air_valid);
    CHECK(v.aqi == 2 && v.tvoc_ppb == 120 && v.eco2_ppm == 650);
    CHECK(sim_log_count("ENS160: warm-up") == 1);
    CHECK(sim_log_contains("ENS160: normal operation"));
    CHECK(g_sim.ens.opmode_writes.size() == 2); // started once
}

[[maybe_unused]] static void test_aht21_unplugged_and_back()
{
    const int resets = g_sim.bus_resets;
    const int compensations = g_sim.ens.compensation_writes;
    g_sim.aht.present = false;
    // Two failed readings: the last values are kept
    sensor_values_t v = read_after(10000);
    CHECK(v.climate_valid);
    v = read_after(10000);
    CHECK(v.climate_valid);
    // Third: lost
    v = read_after(10000);
    CHECK(!v.climate_valid);
    CHECK(sim_log_contains("AHT21 lost after 3 readings without values"));
    sensors_log(&v);
    CHECK(sim_log_contains("sensors: no AHT21 | AQI 2"));
    CHECK(g_sim.bus_resets == resets + 1);
    // Searched for at every reading, the absence logged once; the ENS160 keeps working without compensation
    for (int i = 0; i < 5; i++) {
        v = read_after(10000);
        CHECK(!v.climate_valid);
        CHECK(v.air_valid);
    }
    CHECK(sim_log_count("No AHT21 at 0x38") == 1);
    CHECK(g_sim.ens.compensation_writes == compensations + 2);

    // Plugged in again (powered up again: needs the register initialization)
    g_sim.aht.present = true;
    g_sim.aht.status_after_reset = 0x00;
    g_sim.aht.temperature_raw = 0x63333; // 27.5 °C
    v = read_after(10000);
    CHECK(v.climate_valid);
    CHECK_NEAR(v.temperature_c, expected_temperature(), 0.001);
    // Below 0 °C
    g_sim.aht.temperature_raw = 0x39999; // -5.0 °C
    v = read_after(10000);
    sensors_log(&v);
    CHECK(sim_log_contains(kOffset == 0 ? "sensors: -5.00 C" : "sensors: -7.00 C"));
    CHECK(g_sim.aht.initialized_regs.size() == 3);
    CHECK(g_sim.ens.temp_in == ens160_temp_in(expected_temperature()));
}

[[maybe_unused]] static void test_aht21_glitch()
{
    // One corrupted frame: no new value, the last one is kept, nothing logged as lost
    const int lost = sim_log_count("AHT21 lost");
    g_sim.aht.corrupt_crc = true;
    sensor_values_t v = read_after(10000);
    CHECK(v.climate_valid);
    g_sim.aht.corrupt_crc = false;
    v = read_after(10000);
    CHECK(v.climate_valid);
    CHECK(sim_log_count("AHT21 lost") == lost);

    // The sensor lost its calibration (power glitch): initialized again at the next reading, values kept
    g_sim.aht.status = 0x00;
    g_sim.aht.status_after_reset = 0x18;
    v = read_after(10000);
    CHECK(v.climate_valid);
    const size_t trace = g_sim.i2c.size();
    v = read_after(10000);
    CHECK(v.climate_valid);
    CHECK(sim_i2c_find("W38 BA", trace) >= 0); // soft reset of the new initialization
}

[[maybe_unused]] static void test_aht21_never_calibrated()
{
    // Initializes but its frames never carry the calibration bit: initialized again at every reading, and its last
    // values are dropped after 3 readings, not kept forever
    g_sim.aht.calibrates = false;
    g_sim.aht.status_after_reset = 0x10;
    g_sim.aht.status = 0x10;
    sensor_values_t v = read_after(10000);
    CHECK(v.climate_valid);
    v = read_after(10000);
    CHECK(v.climate_valid);
    v = read_after(10000);
    CHECK(!v.climate_valid);
    for (int i = 0; i < 3; i++) {
        v = read_after(10000);
        CHECK(!v.climate_valid);
    }
    CHECK(sim_log_count("AHT21 lost after 3 readings without values") == 2); // the unplugged test lost it once
    CHECK(sim_log_count("AHT21 found at 0x38") == 2);                       // boot, plugged in again: no repeats
    g_sim.aht.calibrates = true;
    g_sim.aht.status_after_reset = 0x18;
    v = read_after(10000);
    CHECK(v.climate_valid);
}

[[maybe_unused]] static void test_ens160_stuck()
{
    // Stops measuring: values kept for two readings, then unknown, then reset and started again
    g_sim.ens.stuck = true;
    sensor_values_t v = read_after(10000);
    CHECK(v.air_valid);
    v = read_after(10000);
    CHECK(v.air_valid);
    v = read_after(10000);
    CHECK(!v.air_valid && !v.air_found);
    CHECK(sim_log_contains("ENS160 lost after 3 readings without values"));
    const size_t writes = g_sim.ens.opmode_writes.size();
    v = read_after(10000);
    CHECK(g_sim.ens.opmode_writes.size() == writes + 3);
    CHECK(g_sim.ens.opmode_writes[writes] == 0xF0); // reset first
    CHECK(v.air_found && !v.air_valid);             // just started
    v = read_after(10000);
    CHECK(v.air_found);
    CHECK(v.air_valid == kReportWhileWarmingUp); // warms up again
    CHECK(sim_log_count("ENS160: warm-up") == 2);

    // Error flag: handled as missing values
    for (int i = 0; i < 18; i++) {
        v = read_after(10000);
    }
    CHECK(v.air_valid);
    g_sim.ens.error = true;
    v = read_after(10000);
    CHECK(v.air_valid);
    CHECK(sim_log_contains("ENS160 reports an error"));
    v = read_after(10000);
    v = read_after(10000);
    CHECK(!v.air_valid);
    v = read_after(10000); // reset clears the error
    CHECK(!g_sim.ens.error);
}

[[maybe_unused]] static void test_ens160_invalid_output()
{
    // Validity 3 (invalid output): not sent, whatever the option, and logged
    g_sim.ens.warm_up_ms = 0; // past the warm-up
    sensor_values_t v = read_after(10000);
    CHECK(v.air_valid);
    g_sim.ens.invalid = true;
    v = read_after(10000);
    CHECK(v.air_found && !v.air_valid);
    CHECK(sim_log_contains("ENS160: invalid output, air quality unknown"));
    g_sim.ens.invalid = false;
    v = read_after(10000);
    CHECK(v.air_valid);
}

[[maybe_unused]] static void test_no_aht21_from_boot()
{
    sim_reset();
    g_sim.aht.present = false;
    g_sim.ens.initial_start_up = true; // a new ENS160: first hour
    g_sim.ens.warm_up_ms = 3600000;
    CHECK(sensors_init_bus() == ESP_OK);
    sensor_values_t v = {};
    for (int i = 0; i < 10; i++) {
        v = read_after(10000);
        CHECK(!v.climate_valid);
    }
    CHECK(sim_log_count("No AHT21 at 0x38") == 1);
    CHECK(sim_log_contains("The ENS160 measures no temperature"));
    CHECK(g_sim.ens.compensation_writes == 0);
    CHECK(v.air_found);
    CHECK(v.air_valid == kReportWhileWarmingUp);
    CHECK(sim_log_contains("ENS160: initial start-up"));
}

int main(int argc, char **argv)
{
    sim_parse_args(argc, argv);
#if NO_AHT21
    test_no_aht21_from_boot();
#else
    sim_reset();
    test_bus();
    test_start_and_warm_up();
    test_aht21_unplugged_and_back();
    test_aht21_glitch();
    test_aht21_never_calibrated();
    test_ens160_stuck();
    test_ens160_invalid_output();
#endif
    return sim_summary("sensors");
}
