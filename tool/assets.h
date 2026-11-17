#ifndef VF_ASSETS_H
#define VF_ASSETS_H

#include "asset_table.h"
#include "rom.h"
#include <stddef.h>
#include <stdint.h>

int assets_patch(const region_t *region, const rom_t *rom, uint8_t **out, size_t *out_size,
		 unsigned *patched, char *err, size_t err_size);

#endif
