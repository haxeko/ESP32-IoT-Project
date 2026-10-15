#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
typedef int gpio_num_t;
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
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
    *bus = (void *)1;
    return ESP_OK;
}
static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *,
                                                  i2c_master_dev_handle_t *dev)
{
    *dev = (void *)1;
    return ESP_OK;
}
static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *data, size_t len, int)
{
    return sim_oled_write(data, len);
}
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int) { return ESP_FAIL; }
