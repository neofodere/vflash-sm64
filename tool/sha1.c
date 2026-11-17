#include "sha1.h"
#include <string.h>

#define ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++)
		w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; i++)
	{
		uint32_t f, k;
		if (i < 20)
			f = (b & c) | (~b & d), k = 0x5A827999;
		else if (i < 40)
			f = b ^ c ^ d, k = 0x6ED9EBA1;
		else if (i < 60)
			f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
		else
			f = b ^ c ^ d, k = 0xCA62C1D6;
		uint32_t t = ROL(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = ROL(b, 30);
		b = a;
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
}

void sha1(const uint8_t *data, size_t size, uint8_t out[20])
{
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	size_t i = 0;
	for (; i + 64 <= size; i += 64)
		block(h, data + i);
	uint8_t tail[128] = { 0 };
	size_t rem = size - i;
	memcpy(tail, data + i, rem);
	tail[rem] = 0x80;
	size_t len = rem < 56 ? 64 : 128;
	uint64_t bits = (uint64_t)size * 8;
	for (int k = 0; k < 8; k++)
		tail[len - 1 - k] = (uint8_t)(bits >> (8 * k));
	block(h, tail);
	if (len == 128)
		block(h, tail + 64);
	for (int k = 0; k < 5; k++)
	{
		out[4 * k] = (uint8_t)(h[k] >> 24);
		out[4 * k + 1] = (uint8_t)(h[k] >> 16);
		out[4 * k + 2] = (uint8_t)(h[k] >> 8);
		out[4 * k + 3] = (uint8_t)h[k];
	}
}

void sha1_hex(const uint8_t digest[20], char out[41])
{
	for (int i = 0; i < 20; i++)
	{
		out[2 * i] = "0123456789abcdef"[digest[i] >> 4];
		out[2 * i + 1] = "0123456789abcdef"[digest[i] & 15];
	}
	out[40] = 0;
}
