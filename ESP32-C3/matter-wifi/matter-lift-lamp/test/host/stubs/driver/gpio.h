// Host stub of ESP-IDF driver/gpio.h: configuration and levels go to the simulated GPIO of the test, which defines
// sim_gpio_config() and sim_gpio_set_level().
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;
typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT = 1,
    GPIO_MODE_OUTPUT = 2,
    GPIO_MODE_INPUT_OUTPUT = 3,
} gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE = 0 } gpio_int_type_t;

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t sim_gpio_config(const gpio_config_t *cfg);
esp_err_t sim_gpio_set_level(int gpio, uint32_t level);

static inline esp_err_t gpio_config(const gpio_config_t *cfg)
{
    return sim_gpio_config(cfg);
}

static inline esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    return sim_gpio_set_level(gpio, level);
}
