#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "sim.h"
typedef int gpio_num_t;
#define GPIO_IS_VALID_GPIO(gpio) ((gpio) >= 0 && (gpio) <= 21)
enum { GPIO_MODE_INPUT = 1, GPIO_PULLUP_ENABLE = 1, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0 };
typedef struct { uint64_t pin_bit_mask; int mode; int pull_up_en; int pull_down_en; int intr_type; } gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t *) { return ESP_OK; }
static inline int gpio_get_level(gpio_num_t) { return g_sim.gpio_level; }
