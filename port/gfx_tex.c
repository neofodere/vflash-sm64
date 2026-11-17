#include <ultra64.h>
#include "gfx.h"
#include "vf_port.h"

#define CELL 32
#define CELLS_X 32
#define CELLS_Y 20
#define MAX_ENTRIES 256
#define MAX_TEXELS 32768
#define HASH_SAMPLES 32

typedef struct {
	const u8 *addr;
	u32 hash;
	u32 params;
	u32 used_frame;
	GeTexture tex;
	u8 cx;
	u8 cy;
	u8 cw;
	u8 ch;
	u8 live;
} CacheEntry;

static CacheEntry sEntries[MAX_ENTRIES];
static u8 sCells[CELLS_Y][CELLS_X];
static u16 sScratch[MAX_TEXELS];
static int sInited;

static u32 log2_ceil8(u32 v)
{
	u32 n = 0;

	while ((8u << n) < v)
		n++;
	return n;
}

static u32 sample_hash(const TexSource *src, u32 bytes)
{
	u32 h = 2166136261u;
	u32 step = (bytes / HASH_SAMPLES) & ~3u;
	u32 i;

	if (step == 0)
		step = 4;
	for (i = 0; i + 4 <= bytes; i += step) {
		const u8 *p = src->data + i;

		h = (h ^ p[0]) * 16777619u;
		h = (h ^ p[1]) * 16777619u;
		h = (h ^ p[2]) * 16777619u;
		h = (h ^ p[3]) * 16777619u;
	}
	return h;
}

static u16 pack555(u32 r, u32 g, u32 b)
{
	u16 p = (u16) (r | g << 5 | b << 10);

	return p ? p : 0x0421;
}

static u16 gray555(u32 g5, u32 opaque)
{
	return opaque ? pack555(g5, g5, g5) : 0;
}

static u16 texel_at(const TexSource *s, u32 x, u32 y)
{
	const u8 *row = s->data + y * s->stride;

	switch (s->fmt) {
	case G_IM_FMT_RGBA:
		if (s->siz == G_IM_SIZ_16b) {
			u32 p = (u32) row[x * 2] << 8 | row[x * 2 + 1];

			if (!(p & 1))
				return 0;
			return pack555(p >> 11 & 31, p >> 6 & 31, p >> 1 & 31);
		}
		if (s->siz == G_IM_SIZ_32b) {
			const u8 *p = row + x * 4;

			if (p[3] < 128)
				return 0;
			return pack555(p[0] >> 3, p[1] >> 3, p[2] >> 3);
		}
		break;
	case G_IM_FMT_IA:
		if (s->siz == G_IM_SIZ_16b)
			return gray555(row[x * 2] >> 3, row[x * 2 + 1] >= 128);
		if (s->siz == G_IM_SIZ_8b) {
			u32 b = row[x];
			u32 i = b >> 4;

			return gray555(i << 1 | i >> 3, (b & 15) >= 8);
		} else {
			u32 b = row[x >> 1];
			u32 n = (x & 1) ? (b & 15) : (b >> 4);
			u32 i = n >> 1;

			return gray555(i << 2 | i >> 1, n & 1);
		}
	case G_IM_FMT_I:
		if (s->siz == G_IM_SIZ_8b)
			return gray555(row[x] >> 3, 1);
		if (s->siz == G_IM_SIZ_4b) {
			u32 b = row[x >> 1];
			u32 n = (x & 1) ? (b & 15) : (b >> 4);

			return gray555(n << 1 | n >> 3, 1);
		}
		break;
	default:
		break;
	}
	return 0x7FFF;
}

static void evict_stale(u32 frame)
{
	int i;

	for (i = 0; i < MAX_ENTRIES; i++) {
		CacheEntry *e = &sEntries[i];
		u32 y;
		u32 x;

		if (!e->live || e->used_frame == frame)
			continue;
		for (y = 0; y < e->ch; y++)
			for (x = 0; x < e->cw; x++)
				sCells[e->cy + y][e->cx + x] = 0;
		e->live = 0;
	}
}

static int find_cells(u32 cw, u32 ch, u32 *cx, u32 *cy)
{
	u32 y;
	u32 x;
	u32 dy;
	u32 dx;

	for (y = 0; y + ch <= CELLS_Y; y++) {
		for (x = 0; x + cw <= CELLS_X; x++) {
			int free_block = 1;

			for (dy = 0; dy < ch && free_block; dy++)
				for (dx = 0; dx < cw; dx++)
					if (sCells[y + dy][x + dx]) {
						free_block = 0;
						break;
					}
			if (free_block) {
				*cx = x;
				*cy = y;
				return 1;
			}
		}
	}
	return 0;
}

static void reset_cache(void)
{
	u32 i;

	for (i = 0; i < MAX_ENTRIES; i++)
		sEntries[i].live = 0;
	for (i = 0; i < CELLS_Y * CELLS_X; i++)
		((u8 *) sCells)[i] = 0;
	sCells[0][0] = 1;
	sInited = 1;
}

void vf_tex_new_frame(void)
{
	if (!sInited)
		reset_cache();
}

static CacheEntry *alloc_entry(u32 cw, u32 ch, u32 frame)
{
	u32 cx;
	u32 cy;
	int tries;
	int i;

	for (tries = 0; tries < 2; tries++) {
		for (i = 0; i < MAX_ENTRIES; i++) {
			CacheEntry *e = &sEntries[i];

			if (e->live)
				continue;
			if (!find_cells(cw, ch, &cx, &cy))
				break;
			e->cx = (u8) cx;
			e->cy = (u8) cy;
			e->cw = (u8) cw;
			e->ch = (u8) ch;
			for (u32 y = 0; y < ch; y++)
				for (u32 x = 0; x < cw; x++)
					sCells[cy + y][cx + x] = 1;
			return e;
		}
		evict_stale(frame);
	}
	return NULL;
}

int vf_tex_acquire(const TexSource *src, GeTexture *out)
{
	u32 frame = vf_gfx_frame();
	u32 bytes = src->height * src->stride;
	u32 params;
	u32 hash;
	u32 pw;
	u32 ph;
	u32 lw;
	u32 lh;
	u32 x;
	u32 y;
	CacheEntry *e;
	int i;

	if (!sInited)
		reset_cache();
	if (src->width == 0 || src->height == 0 || src->width > 256 || src->height > 256)
		return 0;
	lw = log2_ceil8(src->width);
	lh = log2_ceil8(src->height);
	pw = 8u << lw;
	ph = 8u << lh;
	if (pw * ph > MAX_TEXELS)
		return 0;

	params = src->fmt | src->siz << 4 | src->width << 8 | src->height << 18 | src->stride << 24;
	hash = sample_hash(src, bytes);

	for (i = 0; i < MAX_ENTRIES; i++) {
		e = &sEntries[i];
		if (e->live && e->addr == src->data && e->hash == hash && e->params == params) {
			e->used_frame = frame;
			*out = e->tex;
			return 1;
		}
	}

	e = alloc_entry((pw + CELL - 1) / CELL, (ph + CELL - 1) / CELL, frame);
	if (!e)
		return 0;

	for (y = 0; y < ph; y++) {
		u32 sy = y % src->height;

		for (x = 0; x < pw; x++)
			sScratch[y * pw + x] = texel_at(src, x % src->width, sy);
	}

	e->tex.x = (u16) (e->cx * CELL);
	e->tex.y = (u16) (GE_TEX_TOP + e->cy * CELL);
	e->tex.log_w = (u8) lw;
	e->tex.log_h = (u8) lh;
	e->tex.argb4444 = 0;
	e->tex.valid = 1;
	e->addr = src->data;
	e->hash = hash;
	e->params = params;
	e->used_frame = frame;
	e->live = 1;
	ge_tex_store(e->tex.x, e->tex.y, (int) lw, (int) lh, sScratch);
	*out = e->tex;
	return 1;
}
