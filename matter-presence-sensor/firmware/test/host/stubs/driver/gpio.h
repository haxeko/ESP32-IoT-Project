#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
typedef int gpio_num_t;
typedef enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE } gpio_int_type_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t *) { return ESP_OK; }
static inline esp_err_t gpio_reset_pin(gpio_num_t) { return ESP_OK; }
static inline esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t) { return ESP_OK; }
static inline esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    g_sim.gpio_out[pin] = (int)level;
    return ESP_OK;
}
static inline int gpio_get_level(gpio_num_t pin)
{
    auto it = g_sim.gpio_in.find(pin);
    return it == g_sim.gpio_in.end() ? 1 : it->second;
}
