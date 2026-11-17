#include "rom.h"
#include "sha1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t n, const char *msg)
{
	snprintf(err, n, "%s", msg);
	return -1;
}

static void swap_v64(uint8_t *d, size_t n)
{
	for (size_t i = 0; i + 1 < n; i += 2)
	{
		uint8_t t = d[i];
		d[i] = d[i + 1];
		d[i + 1] = t;
	}
}

static void swap_n64(uint8_t *d, size_t n)
{
	for (size_t i = 0; i + 3 < n; i += 4)
	{
		uint8_t a = d[i], b = d[i + 1];
		d[i] = d[i + 3];
		d[i + 1] = d[i + 2];
		d[i + 2] = b;
		d[i + 3] = a;
	}
}

int rom_parse(rom_t *rom, uint8_t *d, size_t size, char *err, size_t err_size)
{
	if (size != ROM_SIZE)
		return fail(err, err_size, "ROM must be exactly 8 MiB");
	if (d[0] == 0x37 && d[1] == 0x80)
		swap_v64(d, size);
	else if (d[0] == 0x40 && d[1] == 0x12)
		swap_n64(d, size);
	else if (!(d[0] == 0x80 && d[1] == 0x37))
		return fail(err, err_size, "file is not an N64 ROM");
	rom->data = d;
	rom->size = size;
	return 0;
}

int rom_load(rom_t *rom, const char *path, char *err, size_t err_size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return fail(err, err_size, "cannot open ROM file");
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size != ROM_SIZE)
	{
		fclose(f);
		return fail(err, err_size, "ROM must be exactly 8 MiB");
	}
	uint8_t *d = malloc((size_t)size);
	if (!d || fread(d, 1, (size_t)size, f) != (size_t)size)
	{
		free(d);
		fclose(f);
		return fail(err, err_size, "cannot read ROM file");
	}
	fclose(f);
	if (rom_parse(rom, d, (size_t)size, err, err_size))
	{
		free(d);
		return -1;
	}
	return 0;
}

void rom_sha1_hex(const rom_t *rom, char hex[41])
{
	uint8_t digest[20];

	sha1(rom->data, rom->size, digest);
	sha1_hex(digest, hex);
}

void rom_free(rom_t *rom)
{
	free(rom->data);
	rom->data = NULL;
	rom->size = 0;
}
