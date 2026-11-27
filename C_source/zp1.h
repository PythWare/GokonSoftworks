#ifndef ZP1_H
#define ZP1_H
#include "util.h"
#define ZP1_PAYLOAD 2048
#define ZP1_ALIGN 128
#define ZP1_CHUNK 0x40000

int zp1_looks_like(const unsigned char *data, size_t len);
uint32_t zp1_original_size(const unsigned char *data, size_t len);
uint32_t zp1_chunk_size(const unsigned char *data, size_t len);
uint32_t zp1_chunk_count(const unsigned char *data, size_t len);
unsigned char zp1_version(const unsigned char *data, size_t len);

int zp1_decompress(const unsigned char *data, size_t len, buf *out, err *e);
int zp1_compress(const unsigned char *data, size_t len, uint32_t chunk,
                 unsigned char version, buf *out, err *e);

#endif
