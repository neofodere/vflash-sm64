#include "assets.h"
#include "asset_table.h"
#include "lz4.h"
#include "mio0.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_OFFSET 0x100
#define TABLE_ENTRIES 240
#define ENTRY_SIZE 16
#define SIZE_OFFSET 4
#define LZ4_HEADER 8
#define ALIGN 16
#define KIND_LZ4 1
#define MARKER_SIZE 8
#define MIO0_CACHE 8
#define SKY_TABLE 320

static const uint8_t marker[4] = { 0x7F, 0x56, 0x46, 0x41 };

typedef struct
{
	uint32_t offset;
	uint32_t stored;
	uint32_t raw;
	uint32_t kind;
} entry_t;

typedef struct
{
	uint32_t offset;
	uint8_t *data;
	size_t size;
} block_t;

typedef struct
{
	const rom_t *rom;
	block_t cache[MIO0_CACHE];
	unsigned next;
	uint8_t *sky;
} source_t;

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static int fail(char *err, size_t n, const char *msg)
{
	snprintf(err, n, "%s", msg);
	return -1;
}

static void source_free(source_t *s)
{
	free(s->sky);
	for (unsigned i = 0; i < MIO0_CACHE; i++)
		free(s->cache[i].data);
}

static const block_t *source_block(source_t *s, const asset_t *a)
{
	for (unsigned i = 0; i < MIO0_CACHE; i++)
	{
		block_t *b = &s->cache[i];

		if (b->data && b->offset == (uint32_t)a->mio0)
			return b;
	}
	block_t *b = &s->cache[s->next++ % MIO0_CACHE];

	free(b->data);
	b->offset = (uint32_t)a->mio0;
	b->data = mio0_decode(s->rom->data, s->rom->size, b->offset, &b->size);
	return b->data ? b : NULL;
}

static const uint8_t *skybox_data(source_t *s, const asset_t *a)
{
	const block_t *b = a->mio0 < 0 ? NULL : source_block(s, a);

	if (!b || b->size < SKY_TABLE || b->size > a->size)
		return NULL;
	free(s->sky);
	s->sky = calloc(a->size, 1);
	if (!s->sky)
		return NULL;
	size_t tiles = b->size - SKY_TABLE;

	for (unsigned i = 0; i < SKY_TABLE / 4; i++)
	{
		uint32_t v = (uint32_t)b->data[tiles + i * 4] << 24 | (uint32_t)b->data[tiles + i * 4 + 1] << 16 |
			     (uint32_t)b->data[tiles + i * 4 + 2] << 8 | b->data[tiles + i * 4 + 3];

		if ((v & 0xFFFFFF) > tiles)
			return NULL;
		wr32(s->sky + i * 4, (v & 0xFFFFFF) + SKY_TABLE);
	}
	memcpy(s->sky + SKY_TABLE, b->data, tiles);
	return s->sky;
}

static const uint8_t *source_data(source_t *s, const asset_t *a)
{
	if (a->kind == ASSET_SKYBOX)
		return skybox_data(s, a);
	if (a->mio0 < 0)
	{
		if (a->pos > s->rom->size || s->rom->size - a->pos < a->size)
			return NULL;
		return s->rom->data + a->pos;
	}
	const block_t *b = source_block(s, a);

	if (!b || a->pos > b->size || b->size - a->pos < a->size)
		return NULL;
	return b->data + a->pos;
}

static int is_marker(const region_t *region, const uint8_t *raw, size_t raw_size, size_t pos, const asset_t **asset)
{
	uint32_t id;

	if (raw_size - pos < MARKER_SIZE || memcmp(raw + pos, marker, sizeof marker))
		return 0;
	id = rd32(raw + pos + 4);
	if (id >= region->count)
		return 0;
	*asset = &region->table[id];
	if (raw_size - pos < (*asset)->size || (*asset)->size < MARKER_SIZE)
		return 0;
	for (size_t i = MARKER_SIZE; i < (*asset)->size; i++)
		if (raw[pos + i])
			return 0;
	return 1;
}

static int patch_segment(const region_t *region, uint8_t *raw, size_t raw_size, source_t *src, uint8_t *seen,
			 int *dirty, char *err, size_t err_size)
{
	size_t pos = 0;

	while (pos + MARKER_SIZE <= raw_size)
	{
		const asset_t *a;
		const uint8_t *data;

		if (raw[pos] != marker[0] || !is_marker(region, raw, raw_size, pos, &a))
		{
			pos++;
			continue;
		}
		data = source_data(src, a);
		if (!data)
		{
			snprintf(err, err_size, "ROM data for asset %u (offset %d/%u) is invalid; is this the right ROM version?",
				 (unsigned)(a - region->table), a->mio0, a->pos);
			return -1;
		}
		memcpy(raw + pos, data, a->size);
		pos += a->size;
		seen[a - region->table] = 1;
		*dirty = 1;
	}
	return 0;
}

static int load_segment(const uint8_t *image, size_t image_size, const entry_t *e, uint8_t **raw, char *err,
			size_t err_size)
{
	*raw = malloc(e->raw ? e->raw : 1);
	if (!*raw)
		return fail(err, err_size, "out of memory");
	if (e->offset > image_size || image_size - e->offset < e->stored)
		return fail(err, err_size, "corrupt game image");
	if (e->kind == KIND_LZ4)
	{
		if (e->stored < LZ4_HEADER || lz4_decompress(image + e->offset + LZ4_HEADER, e->stored - LZ4_HEADER, *raw, e->raw))
			return fail(err, err_size, "corrupt game image");
	}
	else
	{
		if (e->stored != e->raw)
			return fail(err, err_size, "corrupt game image");
		memcpy(*raw, image + e->offset, e->raw);
	}
	return 0;
}

static int store_segment(const uint8_t *raw, entry_t *e, uint8_t **stored, char *err, size_t err_size)
{
	if (e->kind != KIND_LZ4)
	{
		*stored = (uint8_t *)raw;
		e->stored = e->raw;
		return 0;
	}
	*stored = malloc(lz4_bound(e->raw) + LZ4_HEADER);
	if (!*stored)
		return fail(err, err_size, "out of memory");
	memcpy(*stored, "LZ4", 4);
	(*stored)[3] = 0;
	wr32(*stored + 4, e->raw);
	e->stored = (uint32_t)(LZ4_HEADER + lz4_compress(raw, e->raw, *stored + LZ4_HEADER));
	return 0;
}

int assets_patch(const region_t *region, const rom_t *rom, uint8_t **out, size_t *out_size,
		 unsigned *patched, char *err, size_t err_size)
{
	const uint8_t *image = region->image;
	size_t image_size = region->image_size;
	entry_t entries[TABLE_ENTRIES];
	source_t src = { rom, { { 0, NULL, 0 } }, 0, NULL };
	size_t base = image_size;
	size_t capacity;
	size_t used;
	uint8_t *result = NULL;
	uint8_t *seen = calloc(region->count ? region->count : 1, 1);
	int rc = -1;

	*patched = 0;
	if (!seen)
		return fail(err, err_size, "out of memory");
	if (image_size < TABLE_OFFSET + TABLE_ENTRIES * ENTRY_SIZE)
	{
		free(seen);
		return fail(err, err_size, "corrupt game image");
	}
	for (unsigned i = 0; i < TABLE_ENTRIES; i++)
	{
		const uint8_t *p = image + TABLE_OFFSET + i * ENTRY_SIZE;

		entries[i] = (entry_t){ rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12) };
		if (i && entries[i].offset && entries[i].offset < base)
			base = entries[i].offset;
	}
	capacity = image_size + (image_size >> 3) + (size_t)TABLE_ENTRIES * ALIGN;
	result = malloc(capacity);
	if (!result)
	{
		free(seen);
		return fail(err, err_size, "out of memory");
	}
	memcpy(result, image, base);
	used = base;
	for (unsigned i = 1; i < TABLE_ENTRIES; i++)
	{
		entry_t *e = &entries[i];
		uint8_t *raw = NULL;
		uint8_t *stored = NULL;
		int dirty = 0;

		if (!e->offset)
			continue;
		if (load_segment(image, image_size, e, &raw, err, err_size) ||
		    patch_segment(region, raw, e->raw, &src, seen, &dirty, err, err_size))
		{
			free(raw);
			goto done;
		}
		if (dirty)
		{
			if (store_segment(raw, e, &stored, err, err_size))
			{
				free(raw);
				goto done;
			}
		}
		else
		{
			stored = (uint8_t *)image + e->offset;
		}
		if (used + e->stored + ALIGN > capacity)
		{
			size_t grown = capacity * 2 + e->stored;
			uint8_t *bigger = realloc(result, grown);

			if (!bigger)
			{
				if (dirty && stored != raw)
					free(stored);
				free(raw);
				fail(err, err_size, "out of memory");
				goto done;
			}
			result = bigger;
			capacity = grown;
		}
		memcpy(result + used, stored, e->stored);
		e->offset = (uint32_t)used;
		used += e->stored;
		while (used & (ALIGN - 1))
			result[used++] = 0;
		if (dirty && stored != raw)
			free(stored);
		free(raw);
	}
	if (region->sound.ctl_size)
	{
		sound_blobs_t blobs;

		if (sound_build(&region->sound, rom, &blobs, err, err_size))
			goto done;
		for (unsigned i = 0; i < SOUND_BLOBS; i++)
		{
			size_t need = used + blobs.size[i] + ALIGN;

			if (need > capacity)
			{
				uint8_t *bigger = realloc(result, need + (need >> 3));

				if (!bigger)
				{
					sound_free(&blobs);
					fail(err, err_size, "out of memory");
					goto done;
				}
				result = bigger;
				capacity = need + (need >> 3);
			}
			memcpy(result + used, blobs.data[i], blobs.size[i]);
			entries[SOUND_ID_CTL + i] = (entry_t){ (uint32_t)used, (uint32_t)blobs.size[i], (uint32_t)blobs.size[i], 0 };
			used += blobs.size[i];
			while (used & (ALIGN - 1))
				result[used++] = 0;
		}
		sound_free(&blobs);
	}
	for (unsigned i = 1; i < TABLE_ENTRIES; i++)
	{
		uint8_t *p = result + TABLE_OFFSET + i * ENTRY_SIZE;

		wr32(p, entries[i].offset);
		wr32(p + 4, entries[i].stored);
		wr32(p + 8, entries[i].raw);
		wr32(p + 12, entries[i].kind);
	}
	for (unsigned i = 0; i < region->count; i++)
		*patched += seen[i];
	wr32(result + SIZE_OFFSET, (uint32_t)used);
	*out = result;
	*out_size = used;
	result = NULL;
	rc = 0;
done:
	source_free(&src);
	free(seen);
	free(result);
	return rc;
}
