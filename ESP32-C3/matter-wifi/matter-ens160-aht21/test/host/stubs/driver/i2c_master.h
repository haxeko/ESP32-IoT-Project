// I2C master driver of ESP-IDF v6 (esp_driver_i2c), on the simulated bus of sim.h. Transfers to an absent device
// fail as a NACK does.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
typedef int gpio_num_t;
typedef int i2c_port_num_t;
typedef enum { I2C_CLK_SRC_DEFAULT = 0 } i2c_clock_source_t;
typedef enum { I2C_ADDR_BIT_LEN_7 = 0 } i2c_addr_bit_len_t;
struct sim_i2c_bus {
    int unused;
};
struct sim_i2c_dev {
    uint16_t address;
};
typedef sim_i2c_bus *i2c_master_bus_handle_t;
typedef sim_i2c_dev *i2c_master_dev_handle_t;
typedef struct {
    i2c_port_num_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    i2c_clock_source_t clk_source;
    uint8_t glitch_ignore_cnt;
    int intr_priority;
    size_t trans_queue_depth;
    struct {
        uint32_t enable_internal_pullup : 1;
        uint32_t allow_pd : 1;
    } flags;
} i2c_master_bus_config_t;
typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    struct {
        uint32_t disable_ack_check : 1;
    } flags;
} i2c_device_config_t;

static inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus)
{
    static sim_i2c_bus s_bus;
    g_sim.bus_created = true;
    g_sim.sda = config->sda_io_num;
    g_sim.scl = config->scl_io_num;
    g_sim.internal_pullup = config->flags.enable_internal_pullup;
    *bus = &s_bus;
    return ESP_OK;
}
static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *config,
                                                  i2c_master_dev_handle_t *dev)
{
    *dev = new sim_i2c_dev{config->device_address};
    g_sim.devices_added++;
    return ESP_OK;
}
static inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    delete dev;
    return ESP_OK;
}
static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int)
{
    return sim_i2c_write(dev->address, data, len) == 0 ? ESP_OK : ESP_FAIL;
}
static inline esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *data, size_t len, int)
{
    return sim_i2c_read(dev->address, data, len) == 0 ? ESP_OK : ESP_FAIL;
}
static inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *wdata, size_t wlen,
                                                    uint8_t *rdata, size_t rlen, int)
{
    return sim_i2c_write_read(dev->address, wdata, wlen, rdata, rlen) == 0 ? ESP_OK : ESP_FAIL;
}
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t address, int)
{
    return sim_i2c_probe(address) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
static inline esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t)
{
    g_sim.bus_resets++;
    return ESP_OK;
}
