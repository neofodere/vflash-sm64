#include "image.h"
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int image_build(iso_t *iso, const region_t *region, const rom_t *rom, char *err, size_t err_size)
{
	uint8_t *boot;
	size_t boot_size;
	unsigned patched;
	uint8_t dummy[4] = { 0 };
	int r;

	if (assets_patch(region, rom, &boot, &boot_size, &patched, err, err_size))
		return -1;
	if (patched * 10 < region->count * 9)
	{
		free(boot);
		snprintf(err, err_size, "the game image does not match the asset table");
		return -1;
	}
	printf("placed %u of %u ROM assets\n", patched, region->count);
	r = iso_add_file(iso, "0SYSTEM/BOOT.BIN", boot, (uint32_t)boot_size) ||
	    iso_add_file(iso, "0ID_93120_003/0DUMMY50.DAT", dummy, sizeof dummy);
	free(boot);
	if (r)
	{
		snprintf(err, err_size, "cannot add files to the disc image");
		return -1;
	}
	return 0;
}
