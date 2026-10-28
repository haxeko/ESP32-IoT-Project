#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
typedef int gpio_num_t;
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;      // the device address
enum { I2C_CLK_SRC_DEFAULT, I2C_ADDR_BIT_LEN_7 };
typedef struct {
    int i2c_port;
    gpio_num_t sda_io_num, scl_io_num;
    int clk_source;
    int glitch_ignore_cnt;
    struct { int enable_internal_pullup; } flags;
} i2c_master_bus_config_t;
typedef struct { int dev_addr_length; int device_address; int scl_speed_hz; } i2c_device_config_t;
static inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *, i2c_master_bus_handle_t *bus)
{
    if (!g_sim.i2c_bus_ok) {
        return ESP_FAIL;
    }
    g_sim.i2c_buses++;
    *bus = (void *)1;
    return ESP_OK;
}
static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *config,
                                                  i2c_master_dev_handle_t *dev)
{
    *dev = (void *)(intptr_t)config->device_address;
    return ESP_OK;
}
static inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t) { return ESP_OK; }
// The SCD4x simulator answers at SIM_SCD4X_ADDRESS, the OLED at any other address.
static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int)
{
    return (intptr_t)dev == SIM_SCD4X_ADDRESS ? sim_scd4x_write(data, len) : sim_oled_write(data, len);
}
static inline esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *data, size_t len, int)
{
    return (intptr_t)dev == SIM_SCD4X_ADDRESS ? sim_scd4x_read(data, len) : ESP_FAIL;
}
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t address, int)
{
    g_sim.scd4x_probes += address == SIM_SCD4X_ADDRESS;
    return address == SIM_SCD4X_ADDRESS && g_sim.scd4x_present ? ESP_OK : ESP_ERR_NOT_FOUND;
}
