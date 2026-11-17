#include "lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 8

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(*size ? *size : 1);
	if (data && fread(data, 1, *size, f) != *size)
	{
		free(data);
		data = NULL;
	}
	fclose(f);
	return data;
}

int main(int argc, char **argv)
{
	size_t n;
	uint8_t *in;
	uint8_t *out;
	size_t m;
	FILE *f;

	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <input> <output>\n", argv[0]);
		return 2;
	}
	in = read_file(argv[1], &n);
	if (!in)
	{
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 1;
	}
	out = malloc(lz4_bound(n) + HEADER_SIZE);
	if (!out)
		return 1;
	memcpy(out, "LZ4", 4);
	for (int i = 0; i < 4; i++)
		out[4 + i] = (uint8_t)(n >> (8 * i));
	m = lz4_compress(in, n, out + HEADER_SIZE);
	f = fopen(argv[2], "wb");
	if (!f || fwrite(out, 1, m + HEADER_SIZE, f) != m + HEADER_SIZE)
		return 1;
	fclose(f);
	return 0;
}
