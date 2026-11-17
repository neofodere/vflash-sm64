#ifndef ROM_H
#define ROM_H

#include <stddef.h>
#include <stdint.h>

#define ROM_SIZE 0x800000u

typedef struct
{
	uint8_t *data;
	size_t size;
} rom_t;

int rom_parse(rom_t *rom, uint8_t *data, size_t size, char *err, size_t err_size);
int rom_load(rom_t *rom, const char *path, char *err, size_t err_size);
void rom_sha1_hex(const rom_t *rom, char hex[41]);
void rom_free(rom_t *rom);

#endif
