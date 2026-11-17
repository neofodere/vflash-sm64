#ifndef VF_MIO0_H
#define VF_MIO0_H

#include <stddef.h>
#include <stdint.h>

uint8_t *mio0_decode(const uint8_t *rom, size_t rom_size, uint32_t offset, size_t *out_size);

#endif
