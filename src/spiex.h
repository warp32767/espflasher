#ifndef SPIEX_H
#define SPIEX_H

#include <stdint.h>

void spiex_init(void);
void spiex_deinit(void);

uint32_t spiex_read_reg(uint8_t reg);
void spiex_write_reg(uint8_t reg, uint32_t val);

#endif
