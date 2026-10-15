// Host stub of ESP-IDF soc/soc.h: register accesses go to the simulated hardware of the test, which defines
// sim_reg_read() and sim_reg_write().
#pragma once

#include <stdint.h>

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif
#ifndef BIT64
#define BIT64(nr) (1ULL << (nr))
#endif

uint32_t sim_reg_read(uint32_t reg);
void sim_reg_write(uint32_t reg, uint32_t val);

#define REG_READ(r) sim_reg_read((uint32_t)(r))
#define REG_WRITE(r, v) sim_reg_write((uint32_t)(r), (uint32_t)(v))
