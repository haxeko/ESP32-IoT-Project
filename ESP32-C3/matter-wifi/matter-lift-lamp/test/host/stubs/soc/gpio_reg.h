// Host stub of the ESP32-C3 soc/gpio_reg.h registers used by the firmware (same addresses as the chip); REG_READ /
// REG_WRITE (soc/soc.h) send them to the simulated GPIO of the test.
#pragma once

#define DR_REG_GPIO_BASE 0x60004000
#define GPIO_OUT_W1TS_REG (DR_REG_GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC_REG (DR_REG_GPIO_BASE + 0xC)
#define GPIO_IN_REG (DR_REG_GPIO_BASE + 0x3C)
