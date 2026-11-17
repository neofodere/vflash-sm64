#ifndef VF_LZ4_H
#define VF_LZ4_H

#include <stddef.h>
#include <stdint.h>

size_t lz4_bound(size_t n);
size_t lz4_compress(const uint8_t *src, size_t n, uint8_t *dst);
int lz4_decompress(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

#endif
