// Frame decoding and conversions of main/drivers/sensor_math.h.
#include <string.h>

#include "sensor_math.h"
#include "sim.h"

static void test_crc()
{
    // CRC-8/NRSC-5 check value, and the example of the Sensirion datasheets (same polynomial and initial value)
    const char *check = "123456789";
    CHECK(aht2x_crc8((const uint8_t *)check, strlen(check)) == 0xF7);
    const uint8_t beef[2] = {0xBE, 0xEF};
    CHECK(aht2x_crc8(beef, 2) == 0x92);
    CHECK(aht2x_crc8(nullptr, 0) == 0xFF);
}

static void test_aht2x_parse()
{
    uint8_t frame[7];
    float t = 0;
    float rh = 0;

    // 0x80000 = half of the humidity range, 0x66666 = 0.4 of the temperature range (200 x 0.4 - 50 = 30 °C)
    sim_aht21_frame(0x1C, 0x80000, 0x66666, false, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_OK);
    CHECK_NEAR(rh, 50.0, 0.0001);
    CHECK_NEAR(t, 30.0, 0.001);

    // Limits of the 20-bit values
    sim_aht21_frame(0x18, 0xFFFFF, 0x00000, false, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_OK);
    CHECK_NEAR(rh, 100.0, 0.001);
    CHECK_NEAR(t, -50.0, 0.0001);
    sim_aht21_frame(0x18, 0x00000, 0xFFFFF, false, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_OK);
    CHECK_NEAR(rh, 0.0, 0.0001);
    CHECK_NEAR(t, 150.0, 0.001);

    // Nibble shared by humidity (low 4 bits) and temperature (high 4 bits): 23.5 °C, 61.25 %
    const uint32_t rh_raw = (uint32_t)(61.25 / 100 * 1048576);
    const uint32_t t_raw = (uint32_t)((23.5 + 50) / 200 * 1048576);
    sim_aht21_frame(0x1C, rh_raw, t_raw, false, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_OK);
    CHECK_NEAR(rh, 61.25, 0.001);
    CHECK_NEAR(t, 23.5, 0.001);

    // Busy wins over everything (the frame is not complete yet), then the CRC, then the calibration bit
    sim_aht21_frame(0x9C, 0x80000, 0x66666, true, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_BUSY);
    sim_aht21_frame(0x1C, 0x80000, 0x66666, true, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_BAD_CRC);
    sim_aht21_frame(0x10, 0x80000, 0x66666, false, frame);
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_NOT_CALIBRATED);

    // A bus stuck high reads 0xFF everywhere: rejected (busy)
    memset(frame, 0xFF, sizeof(frame));
    CHECK(aht2x_parse(frame, &t, &rh) != AHT2X_FRAME_OK);
    // All zeros: the CRC of six zero bytes is not 0
    memset(frame, 0x00, sizeof(frame));
    CHECK(aht2x_parse(frame, &t, &rh) == AHT2X_FRAME_BAD_CRC);
}

static void test_ens160()
{
    // TEMP_IN = Kelvin x 64, RH_IN = %RH x 512
    CHECK(ens160_temp_in(25.0f) == 19082); // 298.15 x 64 = 19081.6
    CHECK(ens160_temp_in(0.0f) == 17482);  // 273.15 x 64 = 17481.6
    CHECK(ens160_temp_in(-40.0f) == 14922);
    CHECK(ens160_temp_in(NAN) == 0);
    CHECK(ens160_temp_in(-300.0f) == 0);
    CHECK(ens160_temp_in(1000.0f) == 65535);
    CHECK(ens160_rh_in(50.0f) == 25600);
    CHECK(ens160_rh_in(0.0f) == 0);
    CHECK(ens160_rh_in(-3.0f) == 0);
    CHECK(ens160_rh_in(100.0f) == 51200);
    CHECK(ens160_rh_in(120.0f) == 51200);
    CHECK(ens160_rh_in(NAN) == 0);

    CHECK(ens160_validity(0x80) == ENS160_VALIDITY_NORMAL);
    CHECK(ens160_validity(0x86) == ENS160_VALIDITY_WARM_UP);
    CHECK(ens160_validity(0x8A) == ENS160_VALIDITY_INITIAL_START_UP);
    CHECK(ens160_validity(0xCF) == ENS160_VALIDITY_INVALID);

    // AQI-UBA 1..5 -> Good, Fair, Moderate, Poor, VeryPoor (the same numbers in Matter); else Unknown
    CHECK(ens160_aqi_to_air_quality(0) == 0);
    for (uint8_t aqi = 1; aqi <= 5; aqi++) {
        CHECK(ens160_aqi_to_air_quality(aqi) == aqi);
    }
    CHECK(ens160_aqi_to_air_quality(6) == 0);
    CHECK(ens160_aqi_to_air_quality(7) == 0);
}

static void test_matter_values()
{
    CHECK(celsius_to_measured(23.456f, -4000, 12000) == 2346);
    CHECK(celsius_to_measured(-12.346f, -4000, 12000) == -1235);
    CHECK(celsius_to_measured(0.004f, -4000, 12000) == 0);
    CHECK(celsius_to_measured(-0.006f, -4000, 12000) == -1);
    CHECK(celsius_to_measured(200.0f, -4000, 12000) == 12000);
    CHECK(celsius_to_measured(-60.0f, -4000, 12000) == -4000);
    CHECK(celsius_to_measured(NAN, -4000, 12000) == -4000);

    CHECK(humidity_to_measured(45.678f) == 4568);
    CHECK(humidity_to_measured(0.004f) == 0);
    CHECK(humidity_to_measured(101.0f) == 10000);
    CHECK(humidity_to_measured(-1.0f) == 0);
    CHECK(humidity_to_measured(NAN) == 0);

    // The same air 2 °C cooler: about 12.8 % more relative humidity at 25 -> 23 °C
    CHECK_NEAR(humidity_at_temperature(50.0f, 25.0f, 23.0f), 56.38, 0.1);
    CHECK_NEAR(humidity_at_temperature(50.0f, 25.0f, 25.0f), 50.0, 0.0001);
    CHECK_NEAR(humidity_at_temperature(50.0f, 20.0f, 22.0f), 44.24, 0.1);
    CHECK(humidity_at_temperature(95.0f, 25.0f, 20.0f) == 100.0f);
    CHECK(humidity_at_temperature(0.0f, 25.0f, 20.0f) == 0.0f);
}

static void test_reported_value()
{
    ReportedValue r;
    CHECK(r.due(false, 2000, 10)); // nothing written yet
    CHECK(r.due(true, 0, 10));
    r.set(false, 2000);
    CHECK(!r.due(false, 2009, 10));
    CHECK(!r.due(false, 1991, 10));
    CHECK(r.due(false, 2010, 10));
    CHECK(r.due(false, 1990, 10));
    CHECK(r.due(true, 0, 10)); // value -> null
    r.set(true, 1234);
    CHECK(r.value == 0);
    CHECK(!r.due(true, 0, 10));
    CHECK(r.due(false, 0, 10)); // null -> value, even 0
    r.set(false, 3);
    CHECK(!r.due(false, 3, 1));
    CHECK(r.due(false, 4, 1));
}

int main(int argc, char **argv)
{
    sim_parse_args(argc, argv);
    test_crc();
    test_aht2x_parse();
    test_ens160();
    test_matter_values();
    test_reported_value();
    return sim_summary("sensor_math");
}
