#include "lz4.h"
#include <stdlib.h>
#include <string.h>

#define HASH_BITS 16
#define MIN_MATCH 4
#define LAST_LITERALS 5
#define MF_LIMIT 12
#define MAX_OFFSET 65535

size_t lz4_bound(size_t n)
{
	return n + n / 255 + 16;
}

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t hash4(uint32_t v)
{
	return (v * 2654435761u) >> (32 - HASH_BITS);
}

static uint8_t *put_length(uint8_t *out, size_t len)
{
	while (len >= 255)
	{
		*out++ = 255;
		len -= 255;
	}
	*out++ = (uint8_t)len;
	return out;
}

static uint8_t *emit_sequence(uint8_t *out, const uint8_t *lit, size_t lit_len, size_t offset, size_t match_len)
{
	uint8_t *token = out++;

	*token = (uint8_t)((lit_len >= 15 ? 15 : lit_len) << 4);
	if (lit_len >= 15)
		out = put_length(out, lit_len - 15);
	memcpy(out, lit, lit_len);
	out += lit_len;
	if (match_len)
	{
		*out++ = (uint8_t)(offset & 255);
		*out++ = (uint8_t)(offset >> 8);
		match_len -= MIN_MATCH;
		*token |= (uint8_t)(match_len >= 15 ? 15 : match_len);
		if (match_len >= 15)
			out = put_length(out, match_len - 15);
	}
	return out;
}

size_t lz4_compress(const uint8_t *src, size_t n, uint8_t *dst)
{
	uint32_t *table = calloc((size_t)1 << HASH_BITS, sizeof *table);
	const uint8_t *anchor = src;
	size_t pos = 0;
	uint8_t *out = dst;

	if (!table)
		return 0;
	if (n >= MF_LIMIT + 1)
	{
		size_t limit = n - MF_LIMIT;

		while (pos < limit)
		{
			uint32_t h = hash4(read32(src + pos));
			size_t cand = table[h];
			size_t len;

			table[h] = (uint32_t)(pos + 1);
			if (cand == 0 || pos + 1 - cand > MAX_OFFSET || read32(src + cand - 1) != read32(src + pos))
			{
				pos++;
				continue;
			}
			cand--;
			len = MIN_MATCH;
			while (pos + len < n - LAST_LITERALS && src[cand + len] == src[pos + len])
				len++;
			out = emit_sequence(out, anchor, (size_t)(src + pos - anchor), pos - cand, len);
			pos += len;
			anchor = src + pos;
		}
	}
	out = emit_sequence(out, anchor, (size_t)(src + n - anchor), 0, 0);
	free(table);
	return (size_t)(out - dst);
}

static int read_length(const uint8_t **in, const uint8_t *end, size_t *len)
{
	uint8_t b;

	do
	{
		if (*in >= end)
			return -1;
		b = *(*in)++;
		*len += b;
	} while (b == 255);
	return 0;
}

int lz4_decompress(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size)
{
	const uint8_t *in = src;
	const uint8_t *in_end = src + src_size;
	uint8_t *out = dst;
	uint8_t *out_end = dst + dst_size;

	while (in < in_end)
	{
		uint8_t token = *in++;
		size_t lit = token >> 4;
		size_t match = token & 15;
		size_t offset;

		if (lit == 15 && read_length(&in, in_end, &lit))
			return -1;
		if ((size_t)(in_end - in) < lit || (size_t)(out_end - out) < lit)
			return -1;
		memcpy(out, in, lit);
		in += lit;
		out += lit;
		if (in == in_end)
			break;
		if (in_end - in < 2)
			return -1;
		offset = (size_t)in[0] | (size_t)in[1] << 8;
		in += 2;
		if (match == 15 && read_length(&in, in_end, &match))
			return -1;
		match += MIN_MATCH;
		if (offset == 0 || offset > (size_t)(out - dst) || (size_t)(out_end - out) < match)
			return -1;
		while (match--)
		{
			*out = out[-(ptrdiff_t)offset];
			out++;
		}
	}
	return out == out_end ? 0 : -1;
}
