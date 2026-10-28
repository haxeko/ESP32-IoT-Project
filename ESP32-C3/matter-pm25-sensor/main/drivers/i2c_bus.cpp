/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>

#include "i2c_bus.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(int sda_pin, int scl_pin)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_config_t config = {};
    config.i2c_port = -1;
    config.sda_io_num = (gpio_num_t)sda_pin;
    config.scl_io_num = (gpio_num_t)scl_pin;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&config, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
        ESP_LOGE(TAG, "I2C bus on SDA=GPIO%d SCL=GPIO%d failed: %s", sda_pin, scl_pin, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "I2C bus on SDA=GPIO%d SCL=GPIO%d", sda_pin, scl_pin);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get(void)
{
    return s_bus;
}
