#include "mio0.h"
#include <stdlib.h>

#define MIO0_HEADER 16
#define MIO0_MAX_SIZE 0x1000000u

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint8_t *mio0_decode(const uint8_t *rom, size_t rom_size, uint32_t offset, size_t *out_size)
{
	const uint8_t *base;
	uint32_t size;
	uint32_t comp_off;
	uint32_t raw_off;
	uint32_t bit_pos = 0;
	uint32_t comp_pos = 0;
	uint32_t raw_pos = 0;
	uint8_t *out;
	uint32_t o = 0;

	if (offset > rom_size || rom_size - offset < MIO0_HEADER)
		return NULL;
	base = rom + offset;
	if (base[0] != 'M' || base[1] != 'I' || base[2] != 'O' || base[3] != '0')
		return NULL;
	size = be32(base + 4);
	comp_off = be32(base + 8);
	raw_off = be32(base + 12);
	if (size > MIO0_MAX_SIZE || comp_off > rom_size - offset || raw_off > rom_size - offset)
		return NULL;
	out = malloc(size ? size : 1);
	if (!out)
		return NULL;
	while (o < size)
	{
		uint32_t mask_byte = MIO0_HEADER + (bit_pos >> 3);
		int bit;

		if (offset + mask_byte >= rom_size)
			goto bad;
		bit = (base[mask_byte] >> (7 - (bit_pos & 7))) & 1;
		bit_pos++;
		if (bit)
		{
			if (offset + raw_off + raw_pos >= rom_size)
				goto bad;
			out[o++] = base[raw_off + raw_pos++];
		}
		else
		{
			uint32_t pair;
			uint32_t len;
			uint32_t dist;

			if (offset + comp_off + comp_pos + 2 > rom_size)
				goto bad;
			pair = (uint32_t)base[comp_off + comp_pos] << 8 | base[comp_off + comp_pos + 1];
			comp_pos += 2;
			len = (pair >> 12) + 3;
			dist = (pair & 0xFFF) + 1;
			if (dist > o)
				goto bad;
			while (len-- && o < size)
			{
				out[o] = out[o - dist];
				o++;
			}
		}
	}
	*out_size = size;
	return out;
bad:
	free(out);
	return NULL;
}
