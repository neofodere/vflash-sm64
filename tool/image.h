#ifndef IMAGE_H
#define IMAGE_H

#include "asset_table.h"
#include "iso.h"
#include "rom.h"
#include <stddef.h>

int image_build(iso_t *iso, const region_t *region, const rom_t *rom, char *err, size_t err_size);

#endif
