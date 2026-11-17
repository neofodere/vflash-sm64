#include "asset_table.h"
#include "assets.h"
#include "lz4.h"
#include "mio0.h"
#include "rom.h"
#include "sha1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static const asset_t asset_table[] = {
	{ 16, -1, 0x2000, ASSET_RAW },
	{ 12, 0x100, 4, ASSET_RAW },
};

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void hash_of(const char *s, char *hex)
{
	uint8_t d[20];
	sha1((const uint8_t *)s, strlen(s), d);
	sha1_hex(d, hex);
}

static void test_lz4(void)
{
	enum { N = 70000 };
	uint8_t *src = malloc(N);
	uint8_t *packed = malloc(lz4_bound(N));
	uint8_t *back = malloc(N);
	uint32_t x = 12345;

	for (size_t i = 0; i < N; i++)
	{
		x = x * 1664525u + 1013904223u;
		src[i] = (i / 300) % 3 ? (uint8_t)(i / 7) : (uint8_t)(x >> 24);
	}
	size_t m = lz4_compress(src, N, packed);
	CHECK(m > 0 && m < N);
	CHECK(lz4_decompress(packed, m, back, N) == 0 && !memcmp(src, back, N));
	CHECK(lz4_decompress(packed, m, back, N - 1) != 0);
	CHECK(lz4_decompress(packed, m / 2, back, N) != 0);
	free(src);
	free(packed);
	free(back);
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void test_mio0(void)
{
	static const uint8_t block[] = {
		'M', 'I', 'O', '0', 0, 0, 0, 12, 0, 0, 0, 20, 0, 0, 0, 22,
		0xE0, 0x00, 0x00, 0x00,
		0x60, 0x02,
		0xAA, 0xBB, 0xCC,
	};
	uint8_t bad[sizeof block];
	size_t n = 0;
	uint8_t *out = mio0_decode(block, sizeof block, 0, &n);

	CHECK(out && n == 12);
	if (out)
	{
		static const uint8_t expect[12] = { 0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC };

		CHECK(!memcmp(out, expect, 12));
	}
	free(out);
	memcpy(bad, block, sizeof bad);
	bad[0] = 'X';
	CHECK(!mio0_decode(bad, sizeof bad, 0, &n));
	memcpy(bad, block, sizeof bad);
	bad[21] = 0xFF;
	CHECK(!mio0_decode(bad, sizeof bad, 0, &n));
}

static void test_assets(void)
{
	enum { TABLE = 0x100, ENTRIES = 240, BASE = 0x1000, SEG = 4096 };
	uint8_t image[BASE + SEG * 2] = { 0 };
	uint8_t *rom_data = calloc(1, ROM_SIZE);
	uint8_t *packed = malloc(lz4_bound(SEG) + 8);
	uint8_t seg[SEG] = { 0 };
	rom_t rom = { rom_data, ROM_SIZE };
	uint8_t *out;
	size_t out_size;
	unsigned patched;
	char err[256];

	memcpy(image, "BOOT", 4);
	memcpy(seg + 100, "\x7f" "VFA", 4);
	seg[104] = 0;
	memcpy(seg + 1000, "\x7f" "VFA", 4);
	seg[1004] = 1;
	for (int i = 0; i < 16; i++)
		rom_data[0x2000 + i] = (uint8_t)(0x40 + i);
	memcpy(rom_data + 0x100, "MIO0", 4);
	put_be32(rom_data + 0x104, 16);
	put_be32(rom_data + 0x108, 20);
	put_be32(rom_data + 0x10C, 20);
	rom_data[0x110] = 0xFF;
	rom_data[0x111] = 0xFF;
	for (int i = 0; i < 16; i++)
		rom_data[0x114 + i] = (uint8_t)i;
	size_t m = lz4_compress(seg, SEG, packed + 8);
	memcpy(packed, "LZ4", 4);
	packed[3] = 0;
	packed[4] = SEG & 255;
	packed[5] = SEG >> 8;
	packed[6] = packed[7] = 0;
	memcpy(image + BASE, packed, m + 8);
	for (int k = 0; k < 4; k++)
	{
		uint8_t *p = image + TABLE + 16;
		uint32_t v[4] = { BASE, (uint32_t)(m + 8), SEG, 1 };

		p[k] = (uint8_t)(v[0] >> (8 * k));
		p[4 + k] = (uint8_t)(v[1] >> (8 * k));
		p[8 + k] = (uint8_t)(v[2] >> (8 * k));
		p[12 + k] = (uint8_t)(v[3] >> (8 * k));
	}
	region_t region = { "test", "", image, sizeof image, asset_table, 2 };
	CHECK(assets_patch(&region, &rom, &out, &out_size, &patched, err, sizeof err) == 0);
	CHECK(patched == 2);
	if (patched == 2)
	{
		uint8_t back[SEG];
		uint32_t off = (uint32_t)out[TABLE + 16] | (uint32_t)out[TABLE + 17] << 8 | (uint32_t)out[TABLE + 18] << 16;
		uint32_t stored = (uint32_t)out[TABLE + 20] | (uint32_t)out[TABLE + 21] << 8;

		CHECK(off + stored <= out_size);
		CHECK(lz4_decompress(out + off + 8, stored - 8, back, SEG) == 0);
		CHECK(!memcmp(back + 100, rom_data + 0x2000, 16));
		CHECK(!memcmp(back + 1000, rom_data + 0x114 + 4, 12));
		CHECK(back[99] == 0 && back[116] == 0 && back[1012] == 0);
	}
	free(out);
	free(packed);
	free(rom_data);
}

int main(void)
{
	test_lz4();
	test_mio0();
	test_assets();
	char hex[41];
	hash_of("abc", hex);
	CHECK(!strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"));
	hash_of("", hex);
	CHECK(!strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
	hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex);
	CHECK(!strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));

	uint8_t *z64 = malloc(ROM_SIZE), *v64 = malloc(ROM_SIZE), *n64 = malloc(ROM_SIZE);
	for (size_t i = 0; i < ROM_SIZE; i++)
		z64[i] = (uint8_t)(i * 31 + (i >> 8));
	z64[0] = 0x80;
	z64[1] = 0x37;
	z64[2] = 0x12;
	z64[3] = 0x40;
	memcpy(v64, z64, ROM_SIZE);
	memcpy(n64, z64, ROM_SIZE);
	for (size_t i = 0; i < ROM_SIZE; i += 2)
	{
		uint8_t t = v64[i];
		v64[i] = v64[i + 1];
		v64[i + 1] = t;
	}
	for (size_t i = 0; i < ROM_SIZE; i += 4)
	{
		uint8_t t0 = n64[i], t1 = n64[i + 1];
		n64[i] = n64[i + 3];
		n64[i + 1] = n64[i + 2];
		n64[i + 2] = t1;
		n64[i + 3] = t0;
	}
	uint8_t *ref = malloc(ROM_SIZE);
	memcpy(ref, z64, ROM_SIZE);
	rom_t r;
	char err[256];
	CHECK(rom_parse(&r, z64, ROM_SIZE, err, sizeof err) == 0 && !memcmp(r.data, ref, ROM_SIZE));
	CHECK(rom_parse(&r, v64, ROM_SIZE, err, sizeof err) == 0 && !memcmp(r.data, ref, ROM_SIZE));
	CHECK(rom_parse(&r, n64, ROM_SIZE, err, sizeof err) == 0 && !memcmp(r.data, ref, ROM_SIZE));
	ref[0] = 0;
	CHECK(rom_parse(&r, ref, ROM_SIZE, err, sizeof err) != 0);
	CHECK(rom_parse(&r, ref, 1024, err, sizeof err) != 0);
	if (!failures)
		printf("tool tests passed\n");
	return failures != 0;
}
