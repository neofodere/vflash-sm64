#ifndef SOUND_H
#define SOUND_H

#include "asset_table.h"
#include "rom.h"
#include <stddef.h>
#include <stdint.h>

#define SOUND_BLOBS 4

typedef struct
{
	uint8_t *data[SOUND_BLOBS];
	size_t size[SOUND_BLOBS];
} sound_blobs_t;

int sound_build(const sound_t *sound, const rom_t *rom, sound_blobs_t *out, char *err, size_t err_size);
void sound_free(sound_blobs_t *blobs);

#endif
