// AHT21 driver (main/drivers/aht21.cpp) on the simulated bus.
#include "aht21.h"
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

static void test_absent()
{
    sim_reset();
    g_sim.aht.present = false;
    CHECK(aht21_init(bus()) == ESP_ERR_NOT_FOUND);
    float t = 0;
    float rh = 0;
    CHECK(aht21_read(&t, &rh) == ESP_ERR_INVALID_STATE); // never initialized
}

static void test_ready_sensor()
{
    sim_reset();
    CHECK(aht21_init(bus()) == ESP_OK);
    CHECK(sim_i2c_find("W38 BA") >= 0);  // soft reset
    CHECK(sim_i2c_find("W38 1B") < 0);   // status 0x18: no register initialization
    CHECK(g_sim.devices_added == 1);

    // 10 ms or more between the status check and the first measurement command (datasheet)
    const int64_t status_read = sim_i2c_time((int)g_sim.i2c.size() - 1);
    CHECK(g_sim.i2c.back().find("R38 1") != std::string::npos);

    const size_t before = g_sim.i2c.size();
    const int64_t start = sim_ms();
    float t = 0;
    float rh = 0;
    CHECK(aht21_read(&t, &rh) == ESP_OK);
    CHECK_NEAR(t, 30.0, 0.001);
    CHECK_NEAR(rh, 50.0, 0.001);
    // The frame is read 80 ms or more after the measurement command, even with a 10 ms tick
    const int command = sim_i2c_find("W38 AC3300", before);
    const int frame = sim_i2c_find("R38 7", before);
    CHECK(command >= 0 && frame > command);
    CHECK(sim_i2c_time(frame) - sim_i2c_time(command) >= 80);
    CHECK(sim_i2c_time(command) - status_read >= 10);
    CHECK(sim_ms() - start < 120);

    // Initialized again (e.g. after a lost sensor): the same device handle
    CHECK(aht21_init(bus()) == ESP_OK);
    CHECK(g_sim.devices_added == 1);
}

static void test_register_initialization()
{
    sim_reset();
    g_sim.aht.status_after_reset = 0x00; // as after power-on of an AHT21 that needs it
    CHECK(aht21_init(bus()) == ESP_OK);
    // Each register: selected, read back, written with 0xB0 | register and the bytes 1 and 2 read
    const int select_1b = sim_i2c_find("W38 1B0000");
    const int write_1b = sim_i2c_find("W38 BB1234");
    const int select_1c = sim_i2c_find("W38 1C0000");
    const int write_1c = sim_i2c_find("W38 BC1234");
    const int select_1e = sim_i2c_find("W38 1E0000");
    const int write_1e = sim_i2c_find("W38 BE1234");
    CHECK(select_1b >= 0 && write_1b > select_1b && select_1c > write_1b && write_1c > select_1c &&
          select_1e > write_1c && write_1e > select_1e);
    CHECK(sim_i2c_time(write_1b) - sim_i2c_time(select_1b) >= 15); // 5 ms + 10 ms
    CHECK(g_sim.aht.initialized_regs.size() == 3);
    CHECK((g_sim.aht.status & 0x18) == 0x18);
    CHECK(sim_i2c_find("W38 BE0800") < 0); // calibrated by the registers: no AHT20 command

    float t = 0;
    float rh = 0;
    CHECK(aht21_read(&t, &rh) == ESP_OK);
}

static void test_aht20_calibration()
{
    // First AHT20 series: the registers do not set the calibration bit, the 0xBE 0x08 0x00 command does
    sim_reset();
    g_sim.aht.status_after_reset = 0x10;
    g_sim.aht.init_registers = false;
    CHECK(aht21_init(bus()) == ESP_OK);
    CHECK(sim_i2c_find("W38 BE0800") >= 0);
    CHECK((g_sim.aht.status & 0x08) == 0x08);
}

static void test_slow_and_broken_measurements()
{
    float t = 0;
    float rh = 0;

    // Measurement longer than 80 ms: read again every 10 ms
    sim_reset();
    CHECK(aht21_init(bus()) == ESP_OK);
    g_sim.aht.measure_ms = 105;
    CHECK(aht21_read(&t, &rh) == ESP_OK);
    CHECK_NEAR(t, 30.0, 0.001);

    // Never ready
    g_sim.aht.measure_ms = 1000;
    CHECK(aht21_read(&t, &rh) == ESP_ERR_TIMEOUT);

    // Corrupted frame
    g_sim.aht.measure_ms = 75;
    g_sim.aht.corrupt_crc = true;
    CHECK(aht21_read(&t, &rh) == ESP_ERR_INVALID_CRC);
    g_sim.aht.corrupt_crc = false;

    // Lost its calibration (power glitch): the caller initializes it again
    g_sim.aht.status = 0x10;
    CHECK(aht21_read(&t, &rh) == ESP_ERR_INVALID_STATE);
    g_sim.aht.status_after_reset = 0x18;
    CHECK(aht21_init(bus()) == ESP_OK);
    CHECK(aht21_read(&t, &rh) == ESP_OK);

    // Unplugged
    g_sim.aht.present = false;
    CHECK(aht21_read(&t, &rh) == ESP_FAIL);
    CHECK(aht21_init(bus()) == ESP_ERR_NOT_FOUND);
}

int main(int argc, char **argv)
{
    sim_parse_args(argc, argv);
    test_absent();
    test_ready_sensor();
    test_register_initialization();
    test_aht20_calibration();
    test_slow_and_broken_measurements();
    return sim_summary("aht21");
}
