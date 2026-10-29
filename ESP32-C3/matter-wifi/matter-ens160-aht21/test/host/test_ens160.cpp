// ENS160 driver (main/drivers/ens160.cpp) on the simulated bus.
#include "ens160.h"
#include "sensor_math.h"
#include "sim.h"

static i2c_master_bus_handle_t bus()
{
    static i2c_master_bus_handle_t s_bus;
    if (s_bus == nullptr) {
        i2c_master_bus_config_t config = {};
        i2c_new_master_bus(&config, &s_bus);
    }
    return s_bus;
}

static void test_absent_or_wrong_chip()
{
    sim_reset();
    g_sim.ens.present = false;
    CHECK(ens160_init(bus(), false) == ESP_ERR_NOT_FOUND);

    // Another chip at the address: refused, logged once although it is searched for at every reading
    sim_reset();
    g_sim.ens.part_id = 0x1234;
    CHECK(ens160_init(bus(), false) == ESP_ERR_INVALID_RESPONSE);
    CHECK(ens160_init(bus(), false) == ESP_ERR_INVALID_RESPONSE);
    CHECK(sim_log_count("part ID 0x1234") == 1);
    CHECK(g_sim.ens.opmode_writes.empty());

    // ENS161: the same registers
    sim_reset();
    g_sim.ens.part_id = 0x0161;
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(sim_log_contains("ENS161 at 0x53, firmware 5.4.6"));
    CHECK(g_sim.ens.opmode == 0x02);
}

static void test_start_from_power_on()
{
    sim_reset();
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(ens160_address() == 0x53);
    // Deep sleep -> idle -> firmware version -> standard
    CHECK(g_sim.ens.opmode_writes.size() == 2);
    CHECK(g_sim.ens.opmode_writes[0] == 0x01);
    CHECK(g_sim.ens.opmode_writes[1] == 0x02);
    CHECK(sim_i2c_find("W53 1200") >= 0);      // NOP
    CHECK(sim_i2c_find("W53 12CC") >= 0);      // CLRGPR
    CHECK(sim_i2c_find("W53 120E") >= 0);      // GET_APPVER
    CHECK(sim_log_contains("firmware 5.4.6"));
    CHECK(g_sim.ens.opmode == 0x02);

    // No values before the first second of measurement
    ens160_data_t data = {};
    data.aqi = 9;
    CHECK(ens160_read(&data) == ESP_OK);
    CHECK(!data.new_data);
    CHECK(data.aqi == 9); // left as it was
    CHECK(data.status & kEns160StatusRunning);
    CHECK(ens160_validity(data.status) == ENS160_VALIDITY_WARM_UP);

    sim_advance_ms(1500);
    CHECK(ens160_read(&data) == ESP_OK);
    CHECK(data.new_data);
    CHECK(data.aqi == 2);
    CHECK(data.tvoc_ppb == 120);
    CHECK(data.eco2_ppm == 650);
    // Reading the values cleared NEWDAT
    CHECK(ens160_read(&data) == ESP_OK);
    CHECK(!data.new_data);

    // After the warm-up
    sim_advance_ms(180000);
    g_sim.ens.aqi = 4;
    g_sim.ens.tvoc = 1234;
    g_sim.ens.eco2 = 1800;
    CHECK(ens160_read(&data) == ESP_OK);
    CHECK(data.new_data);
    CHECK(ens160_validity(data.status) == ENS160_VALIDITY_NORMAL);
    CHECK(data.aqi == 4 && data.tvoc_ppb == 1234 && data.eco2_ppm == 1800);
}

static void test_compensation()
{
    sim_reset();
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(ens160_set_compensation(25.0f, 50.0f) == ESP_OK);
    // 298.15 K x 64 = 19082 = 0x4A8A, 50 % x 512 = 25600 = 0x6400, LSB first, from TEMP_IN on
    CHECK(sim_i2c_find("W53 138A4A0064") >= 0);
    CHECK(g_sim.ens.temp_in == 19082);
    CHECK(g_sim.ens.rh_in == 25600);
}

static void test_restart_keeps_sensor_running()
{
    // The ESP32 restarted (OTA, crash) while the sensor kept its power and its warm-up progress
    sim_reset();
    g_sim.ens.opmode = 0x02;
    g_sim.ens.standard_since_ms = -600000; // measuring for 10 minutes
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(g_sim.ens.opmode_writes.empty());
    CHECK(sim_log_contains("already measuring (normal operation)"));

    // Reset requested (it stopped measuring): reset, then started again
    sim_reset();
    g_sim.ens.opmode = 0x02;
    CHECK(ens160_init(bus(), true) == ESP_OK);
    CHECK(g_sim.ens.opmode_writes.size() == 3);
    CHECK(g_sim.ens.opmode_writes[0] == 0xF0);
    CHECK(g_sim.ens.opmode == 0x02);

    // Running but in error: started again
    sim_reset();
    g_sim.ens.opmode = 0x02;
    g_sim.ens.error = true;
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(!g_sim.ens.opmode_writes.empty());
}

static void test_low_address()
{
    // ADD pin low: 0x52. The driver moves its device to the new address.
    sim_reset();
    g_sim.ens.address = 0x52;
    CHECK(ens160_init(bus(), false) == ESP_OK);
    CHECK(ens160_address() == 0x52);
    CHECK(sim_i2c_find("W52 1001") >= 0);
    ens160_data_t data = {};
    sim_advance_ms(1000);
    CHECK(ens160_read(&data) == ESP_OK && data.new_data);
}

static void test_unplugged()
{
    sim_reset();
    CHECK(ens160_init(bus(), false) == ESP_OK);
    g_sim.ens.present = false;
    ens160_data_t data = {};
    CHECK(ens160_read(&data) == ESP_FAIL);
    CHECK(ens160_set_compensation(20.0f, 40.0f) == ESP_FAIL);
}

int main(int argc, char **argv)
{
    sim_parse_args(argc, argv);
    test_absent_or_wrong_chip();
    test_start_from_power_on();
    test_compensation();
    test_restart_keeps_sensor_running();
    test_low_address();
    test_unplugged();
    return sim_summary("ens160");
}
